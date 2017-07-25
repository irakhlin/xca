/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2009 -2014 Christian Hohnstaedt.
 *
 * All rights reserved.
 */


#include "pki_scard.h"
#include "pass_info.h"
#include "pk11_attribute.h"
#include "exception.h"
#include "db_base.h"
#include "pkcs11.h"
#include "x509name.h"
#include "func.h"
#include "db.h"
#include "widgets/MainWindow.h"

#include <openssl/rand.h>
#include <openssl/bn.h>

#include <QProgressDialog>
#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QThread>
#include <ltdl.h>

QPixmap *pki_scard::icon[1] = { NULL };
bool pki_scard::only_token_hashes = false;

void pki_scard::init(void)
{
	class_name = "pki_scard";
	ownPass = ptPin;
	dataVersion = 2;
	pkiType = smartCard;

	card_serial = card_manufacturer = card_label = "";
	card_model = slot_label = "";
}

pki_scard::pki_scard(const QString name)
	:pki_key(name)
{
	init();
}

QString pki_scard::getMsg(msg_type msg)
{
	/*
	 * We do not construct english sentences from fragments
	 * to allow proper translations.
	 *
	 * %1 will be replaced by the name of the smartcard
	 */
	switch (msg) {
	case msg_import: return tr("Successfully imported the token key '%1'");
	case msg_delete: return tr("Delete the token key '%1'?");
	case msg_create: return tr("Successfully created the token key '%1'");
	/* %1: Number of ktemplates; %2: list of templatenames */
	case msg_delete_multi: return tr("Delete the %1 keys: %2?");
	}
	return pki_base::getMsg(msg);
}

EVP_PKEY *pki_scard::load_pubkey(pkcs11 &p11, CK_OBJECT_HANDLE object) const
{
	unsigned long keytype;
	EVP_PKEY *pkey = NULL;

	pk11_attr_ulong type(CKA_KEY_TYPE);
	p11.loadAttribute(type, object);
	keytype = type.getValue();

	switch (keytype) {
	case CKK_RSA: {
		RSA *rsa = RSA_new();

		pk11_attr_data n(CKA_MODULUS);
		p11.loadAttribute(n, object);
		rsa->n = n.getBignum();

		pk11_attr_data e(CKA_PUBLIC_EXPONENT);
		p11.loadAttribute(e, object);
		rsa->e = e.getBignum();

		pkey = EVP_PKEY_new();
		EVP_PKEY_assign_RSA(pkey, rsa);
		break;
	}
	case CKK_DSA: {
		DSA *dsa = DSA_new();

		pk11_attr_data p(CKA_PRIME);
		p11.loadAttribute(p, object);
		dsa->p = p.getBignum();

		pk11_attr_data q(CKA_SUBPRIME);
		p11.loadAttribute(q, object);
		dsa->q = q.getBignum();

		pk11_attr_data g(CKA_BASE);
		p11.loadAttribute(g, object);
		dsa->g = g.getBignum();

		pk11_attr_data pub(CKA_VALUE);
		p11.loadAttribute(pub, object);
		dsa->pub_key = pub.getBignum();

		pkey = EVP_PKEY_new();
		EVP_PKEY_assign_DSA(pkey, dsa);
		break;
	}
#ifndef OPENSSL_NO_EC
	case CKK_EC: {
		QByteArray ba;
		EC_GROUP *group;
		ASN1_OCTET_STRING *os;

		EC_KEY *ec = EC_KEY_new();

		pk11_attr_data grp(CKA_EC_PARAMS);
		p11.loadAttribute(grp, object);
		ba = grp.getData();
		group = (EC_GROUP *)
			d2i_bytearray(D2I_VOID(d2i_ECPKParameters), ba);
		pki_openssl_error();

		EC_GROUP_set_asn1_flag(group, 1);
		EC_KEY_set_group(ec, group);
		pki_openssl_error();

		pk11_attr_data pt(CKA_EC_POINT);
		p11.loadAttribute(pt, object);
		ba = pt.getData();
		os = (ASN1_OCTET_STRING *)
			d2i_bytearray(D2I_VOID(d2i_ASN1_OCTET_STRING), ba);
		pki_openssl_error();

		BIGNUM *bn = BN_bin2bn(os->data, os->length, NULL);
		pki_openssl_error();

		EC_POINT *point = EC_POINT_bn2point(group, bn, NULL, NULL);
		BN_free(bn);
		ASN1_OCTET_STRING_free(os);
		pki_openssl_error();

		EC_KEY_set_public_key(ec, point);
		pki_openssl_error();

		pkey = EVP_PKEY_new();
		EVP_PKEY_assign_EC_KEY(pkey, ec);
		break;
	}
#endif
	default:
		throw errorEx(QString("Unsupported CKA_KEY_TYPE: %1\n").arg(keytype));
	}

	pki_openssl_error();
	return pkey;
}

void pki_scard::load_token(pkcs11 &p11, CK_OBJECT_HANDLE object)
{
	tkInfo ti = p11.tokenInfo();
	card_label = ti.label();
	card_manufacturer = ti.manufacturerID();
	card_serial = ti.serial();
	card_model = ti.model();

	pk11_attr_data id(CKA_ID);
	p11.loadAttribute(id, object);
	if (id.getAttribute()->ulValueLen > 0) {
		BIGNUM *cka_id = id.getBignum();
		object_id = BNoneLine(cka_id);
		BN_free(cka_id);
	}

	try {
		pk11_attr_data label(CKA_LABEL);
		p11.loadAttribute(label, object);
		slot_label = label.getText();
	} catch (errorEx &err) {
		printf("No PubKey Label: %s\n", err.getCString());
		// ignore
	}
	if (slot_label.isEmpty()) {
		try{
			x509name xn;

			pk11_attr_data subj(CKA_SUBJECT);
			p11.loadAttribute(subj, object);
			QByteArray der = subj.getData();
			xn.d2i(der);
			slot_label = xn.getMostPopular();
			pki_openssl_error();
		} catch (errorEx &err) {
			printf("No Pubkey Subject: %s\n", err.getCString());
			// ignore
		}
	}
	EVP_PKEY *pkey = load_pubkey(p11, object);
	if (pkey) {
		if (key)
			EVP_PKEY_free(key);
		key = pkey;
	}
	setIntName(slot_label);
	pki_openssl_error();
}

pk11_attr_data pki_scard::getIdAttr() const
{
	pk11_attr_data id(CKA_ID);
	if (object_id.isEmpty())
		return id;
	BIGNUM *bn = NULL;
	BN_hex2bn(&bn, CCHAR(object_id));
	id.setBignum(bn, true);
	return id;
}

void pki_scard::deleteFromToken()
{
	slotid slot;

	if (!prepare_card(&slot))
		return;
	deleteFromToken(slot);
}

pk11_attlist pki_scard::objectAttributesNoId(EVP_PKEY *pk, bool priv) const
{
	QByteArray ba;
	RSA *rsa = pk->pkey.rsa;
	DSA *dsa = pk->pkey.dsa;
#ifndef OPENSSL_NO_EC
	EC_KEY *ec = pk->pkey.ec;
#endif

	pk11_attlist attrs(pk11_attr_ulong(CKA_CLASS,
			priv ? CKO_PRIVATE_KEY : CKO_PUBLIC_KEY));

	switch (EVP_PKEY_type(pk->type)) {
	case EVP_PKEY_RSA:
		attrs << pk11_attr_ulong(CKA_KEY_TYPE, CKK_RSA) <<
			pk11_attr_data(CKA_MODULUS, rsa->n, false) <<
			pk11_attr_data(CKA_PUBLIC_EXPONENT, rsa->e, false);
		break;
	case EVP_PKEY_DSA:
		attrs << pk11_attr_ulong(CKA_KEY_TYPE, CKK_DSA) <<
			pk11_attr_data(CKA_PRIME, dsa->p, false) <<
			pk11_attr_data(CKA_SUBPRIME, dsa->q, false) <<
			pk11_attr_data(CKA_BASE, dsa->g, false);
		break;
#ifndef OPENSSL_NO_EC
	case EVP_PKEY_EC:
		ba = i2d_bytearray(I2D_VOID(i2d_ECPKParameters),
				EC_KEY_get0_group(ec));

		attrs << pk11_attr_ulong(CKA_KEY_TYPE, CKK_EC) <<
			pk11_attr_data(CKA_EC_PARAMS, ba);
		break;
#endif
	default:
		throw errorEx(QString("Unkown Keytype %d").arg(pk->type));

	}
	return attrs;
}

pk11_attlist pki_scard::objectAttributes(bool priv) const
{

	pk11_attlist attrs = objectAttributesNoId(key, priv);
	attrs << getIdAttr();
	return attrs;
}

void pki_scard::deleteFromToken(slotid slot)
{
	pkcs11 p11;
	p11.startSession(slot, true);

	tkInfo ti = p11.tokenInfo();
	if (!XCA_YESNO(tr("Delete the private key '%1' from the token '%2 (#%3)' ?").
			arg(getIntName()).arg(ti.label()).arg(ti.serial())))
		return;

	if (p11.tokenLogin(card_label, false).isNull())
		return;

	pk11_attlist atts = objectAttributes(true);
	QList<CK_OBJECT_HANDLE> priv_objects = p11.objectList(atts);
	atts = objectAttributes(false);
	QList<CK_OBJECT_HANDLE> pub_objects = p11.objectList(atts);

	p11.deleteObjects(priv_objects);
	p11.deleteObjects(pub_objects);
}

int pki_scard::renameOnToken(slotid slot, QString name)
{
	pkcs11 p11;
	p11.startSession(slot, true);
	QList<CK_OBJECT_HANDLE> objs;

	if (p11.tokenLogin(card_label, false).isNull())
		return 0;
	pk11_attr_data label(CKA_LABEL, name.toUtf8());

	/* Private key */
	pk11_attlist attrs = objectAttributes(true);

	objs = p11.objectList(attrs);
	if (!objs.count())
		return 0;
	p11.storeAttribute(label, objs[0]);

	/* Public key */
	attrs = objectAttributes(false);
	objs = p11.objectList(attrs);
	if (objs.count())
		p11.storeAttribute(label, objs[0]);

	return 1;
}

void pki_scard::store_token(slotid slot, EVP_PKEY *pkey)
{
	QByteArray ba;
	RSA *rsa = pkey->pkey.rsa;
	DSA *dsa = pkey->pkey.dsa;
#ifndef OPENSSL_NO_EC
	EC_KEY *ec = pkey->pkey.ec;
#endif
	pk11_attlist pub_atts;
	pk11_attlist priv_atts;
	QList<CK_OBJECT_HANDLE> objects;

	pub_atts = objectAttributesNoId(pkey, false);
	priv_atts = objectAttributesNoId(pkey, true);

	pkcs11 p11;
	p11.startSession(slot, true);

	QList<CK_OBJECT_HANDLE> objs = p11.objectList(pub_atts);
	if (objs.count() == 0)
		objs = p11.objectList(priv_atts);
	if (objs.count() != 0) {
		XCA_INFO(tr("This Key is already on the token"));
		load_token(p11, objs[0]);
		return;
	}
	pk11_attr_data new_id = p11.findUniqueID(CKO_PUBLIC_KEY);

	pub_atts << new_id <<
		pk11_attr_bool(CKA_TOKEN, true) <<
		pk11_attr_data(CKA_LABEL, getIntName().toUtf8()) <<
		pk11_attr_bool(CKA_PRIVATE, false) <<
		pk11_attr_bool(CKA_WRAP, true) <<
		pk11_attr_bool(CKA_ENCRYPT, true) <<
		pk11_attr_bool(CKA_VERIFY, true);

	priv_atts << new_id <<
		pk11_attr_bool(CKA_TOKEN, true) <<
		pk11_attr_data(CKA_LABEL, desc.toUtf8()) <<
		pk11_attr_bool(CKA_PRIVATE, true) <<
		pk11_attr_bool(CKA_UNWRAP, true) <<
		pk11_attr_bool(CKA_DECRYPT, true) <<
		pk11_attr_bool(CKA_SIGN, true);

	switch (EVP_PKEY_type(pkey->type)) {
	case EVP_PKEY_RSA:
		priv_atts <<
		pk11_attr_data(CKA_PRIVATE_EXPONENT, rsa->d, false) <<
		pk11_attr_data(CKA_PRIME_1, rsa->p, false) <<
		pk11_attr_data(CKA_PRIME_2, rsa->q, false) <<
		pk11_attr_data(CKA_EXPONENT_1, rsa->dmp1, false) <<
		pk11_attr_data(CKA_EXPONENT_2, rsa->dmq1, false) <<
		pk11_attr_data(CKA_COEFFICIENT, rsa->iqmp, false);
		break;
	case EVP_PKEY_DSA:
		priv_atts << pk11_attr_data(CKA_VALUE, dsa->priv_key, false);
		pub_atts << pk11_attr_data(CKA_VALUE, dsa->pub_key, false);
		break;
#ifndef OPENSSL_NO_EC
	case EVP_PKEY_EC: {
		/* Public Key */
		BIGNUM *point;
		int size;
		unsigned char *buf;
		ASN1_OCTET_STRING *os;
		point = EC_POINT_point2bn(EC_KEY_get0_group(ec),
			EC_KEY_get0_public_key(ec),
			EC_KEY_get_conv_form(ec), NULL, NULL);

		pki_openssl_error();
		size = BN_num_bytes(point);
		buf = (unsigned char *)OPENSSL_malloc(size);
		check_oom(buf);
		BN_bn2bin(point, buf);
		os = ASN1_OCTET_STRING_new();
		/* set0 -> ASN1_OCTET_STRING_free() also free()s buf */
		ASN1_STRING_set0(os, buf, size);
		ba = i2d_bytearray(I2D_VOID(i2d_ASN1_OCTET_STRING), os);
		ASN1_OCTET_STRING_free(os);
		BN_free(point);
		pki_openssl_error();
		pub_atts << pk11_attr_data(CKA_EC_POINT, ba);

		/* Private key */
		priv_atts << pk11_attr_data(CKA_VALUE,
					EC_KEY_get0_private_key(ec));
		break;
	}
#endif
	default:
		throw errorEx(QString("Unkown Keytype %d").arg(pkey->type));

	}


	tkInfo ti = p11.tokenInfo();
	if (p11.tokenLogin(ti.label(), false).isNull())
		throw errorEx(tr("PIN input aborted"));

	p11.createObject(pub_atts);
	p11.createObject(priv_atts);

	pub_atts.reset();

	pub_atts = objectAttributesNoId(pkey, false);
	pub_atts << new_id;

	objs = p11.objectList(pub_atts);
	if (objs.count() == 0)
		throw errorEx(tr("Unable to find copied key on the token"));

	load_token(p11, objs[0]);
}

QList<int> pki_scard::possibleHashNids()
{
	QList<int> nids;

	if (!only_token_hashes)
		return pki_key::possibleHashNids();

	foreach(CK_MECHANISM_TYPE mechanism, mech_list) {
		switch (EVP_PKEY_type(key->type)) {
		case EVP_PKEY_RSA:
			switch (mechanism) {
			case CKM_MD5_RSA_PKCS:    nids << NID_md5; break;
			case CKM_SHA1_RSA_PKCS:   nids << NID_sha1; break;
			case CKM_SHA256_RSA_PKCS: nids << NID_sha256; break;
			case CKM_SHA384_RSA_PKCS: nids << NID_sha384; break;
			case CKM_SHA512_RSA_PKCS: nids << NID_sha512; break;
			case CKM_RIPEMD160_RSA_PKCS: nids << NID_ripemd160; break;
			}
			break;
		case EVP_PKEY_DSA:
			switch (mechanism) {
			case CKM_DSA_SHA1:        nids << NID_sha1; break;
			}
			break;
#ifndef OPENSSL_NO_EC
		case EVP_PKEY_EC:
			switch (mechanism) {
			case CKM_ECDSA_SHA1:      nids << NID_sha1; break;
			}
			break;
#endif
		}
	}
	if (nids.count() == 0) {
		switch (EVP_PKEY_type(key->type)) {
		case EVP_PKEY_RSA:
			nids << NID_md5 << NID_sha1 << NID_sha256 <<
				NID_sha384 << NID_sha512 << NID_ripemd160;
			break;
		case EVP_PKEY_DSA:
#ifndef OPENSSL_NO_EC
		case EVP_PKEY_EC:
#endif
			nids << NID_sha1;
			break;
		}
	}
	return nids;
}

const EVP_MD *pki_scard::getDefaultMD()
{
	if (mech_list.contains(CKM_SHA1_RSA_PKCS))
		return EVP_sha1();
	if (mech_list.contains(CKM_DSA_SHA1))
		return EVP_dss1();
#ifndef OPENSSL_NO_EC
	if (mech_list.contains(CKM_ECDSA_SHA1))
		return EVP_ecdsa();
#endif
	if (mech_list.contains(CKM_SHA512_RSA_PKCS))
		return EVP_sha512();
	if (mech_list.contains(CKM_SHA384_RSA_PKCS))
		return EVP_sha384();
	if (mech_list.contains(CKM_SHA256_RSA_PKCS))
		return EVP_sha256();
	if (mech_list.contains(CKM_RIPEMD160_RSA_PKCS))
		return EVP_ripemd160();
	if (mech_list.contains(CKM_MD5_RSA_PKCS))
		return EVP_md5();

	/* Last resort */
	return EVP_sha1();
}

/* Assures the correct card is inserted and
 * returns the slot ID in slot true on success */
bool pki_scard::prepare_card(slotid *slot, bool verifyPubkey) const
{
	pkcs11 p11;
	slotidList p11_slots;
	int i;

	if (!pkcs11::loaded())
		return false;
	while (1) {
		p11_slots = p11.getSlotList();
		for (i=0; i<p11_slots.count(); i++) {
			pkcs11 myp11;
			tkInfo ti = myp11.tokenInfo(p11_slots[i]);
			if (ti.label() == card_label &&
			    ti.serial() == card_serial)
			{
				break;
			}
		}
		if (i < p11_slots.count())
			break;
		QString msg = tr("Please insert card: %1 %2 [%3] with Serial: %4").
			arg(card_manufacturer).arg(card_model).
			arg(card_label).arg(card_serial);

		if (!XCA_OKCANCEL(msg)) {
			return false;
		}
	}

	*slot = p11_slots[i];
	if (!verifyPubkey)
		return true;

	QList<CK_OBJECT_HANDLE> objects;

	p11.startSession(p11_slots[i]);

	pk11_attlist cls (pk11_attr_ulong(CKA_CLASS, CKO_PUBLIC_KEY));
	cls << getIdAttr();

	objects = p11.objectList(cls);

	for (int j=0; j< objects.count(); j++) {
		CK_OBJECT_HANDLE object = objects[j];
		EVP_PKEY *pkey = load_pubkey(p11, object);
		if (EVP_PKEY_cmp(key, pkey) == 1)
			return true;
		if (!object_id.isEmpty())
			XCA_WARN(tr("Public Key missmatch. Please re-import card"));
	}
	return false;
}

class keygenThread: public QThread
{
public:
	errorEx err;
	pk11_attr_data id;
	QString name;
	int size;
	int curve_nid;
	int method;
	pkcs11 *p11;

	void run()
	{
		try {
			id = p11->generateKey(name, method, size, curve_nid);
		} catch (errorEx &e) {
			err = e;
		}
	}
};

void pki_scard::generateKey_card(int type, slotid slot, int size,
		int curve_nid, QProgressBar *bar)
{
	pk11_attlist atts;

	pkcs11 p11;
	p11.startSession(slot, true);
	p11.getRandom();

	tkInfo ti = p11.tokenInfo();

	if (p11.tokenLogin(ti.label(), false).isNull())
		return;

	keygenThread kt;
	kt.name = getIntName();
	kt.size = size;
	kt.curve_nid = curve_nid;
	switch (type) {
	case EVP_PKEY_RSA:
		kt.method = CKM_RSA_PKCS_KEY_PAIR_GEN;
		break;
	case EVP_PKEY_DSA:
		kt.method = CKM_DSA_KEY_PAIR_GEN;
		break;
#ifndef OPENSSL_NO_EC
	case EVP_PKEY_EC:
		kt.method = CKM_EC_KEY_PAIR_GEN;
		break;
#endif
	default:
		throw errorEx(tr("Illegal Key generation method"));
	}
	kt.p11 = &p11;
	kt.start();
	while (!kt.wait(20)) {
		inc_progress_bar(0, 0, bar);
	}
	if (!kt.err.isEmpty())
		throw errorEx(kt.err);

	atts << pk11_attr_ulong(CKA_CLASS, CKO_PUBLIC_KEY) << kt.id;
	QList<CK_OBJECT_HANDLE> objects = p11.objectList(atts);
	if (objects.count() != 1)
		printf("OBJECTS found: %d\n",objects.count());

	if (objects.count() == 0)
		throw errorEx(tr("Unable to find generated key on card"));

	load_token(p11, objects[0]);
}

pki_scard::~pki_scard()
{
}

QByteArray pki_scard::toData()
{
	QByteArray ba;

	ba += db::stringToData(card_serial);
	ba += db::stringToData(card_manufacturer);
	ba += db::stringToData(card_label);
	ba += db::stringToData(slot_label);
	ba += db::stringToData(card_model);
	ba += db::stringToData(object_id);
	ba += db::intToData(mech_list.count());
	for (int i=0; i<mech_list.count(); i++)
		ba += db::intToData(mech_list[i]);

	ba += i2d();
	return ba;
}

void pki_scard::fromData(const unsigned char *p, db_header_t *head )
{
	int version, size;

	size = head->len - sizeof(db_header_t);
        version = head->version;

	QByteArray ba((const char*)p, size);

	card_serial = db::stringFromData(ba);
	card_manufacturer = db::stringFromData(ba);
	card_label = db::stringFromData(ba);
	slot_label = db::stringFromData(ba);
	card_model = db::stringFromData(ba);
	if (version < 2)
		card_model.clear();
	object_id  = db::stringFromData(ba);
	int count      = db::intFromData(ba);
	mech_list.clear();
	for (int i=0; i<count; i++)
		mech_list << db::intFromData(ba);

	d2i(ba);
	if (!key || !key->pkey.ptr) {
		throw errorEx(tr("Ignoring unsupported token key"));
	}

	if (ba.count() > 0) {
		my_error(tr("Wrong Size %1").arg(ba.count()));
	}
}

bool pki_scard::isPubKey() const
{
	return false;
}

QString pki_scard::getTypeString(void) const
{
	return tr("Token %1").arg(pki_key::getTypeString());
}

EVP_PKEY *pki_scard::decryptKey() const
{
	slotid slot_id;
	QString pin, key_id;

	if (!prepare_card(&slot_id))
		throw errorEx(tr("Failed to find the key on the token"));

	pkcs11 *p11 = new pkcs11();
	p11->startSession(slot_id);
	pin = p11->tokenLogin(card_label, false);
	if (pin.isNull()) {
		delete p11;
		throw errorEx(tr("Invalid Pin for the token"));
	}
	pk11_attlist atts = objectAttributes(true);
	QList<CK_OBJECT_HANDLE> priv_objects = p11->objectList(atts);
	if (priv_objects.count() != 1) {
		delete p11;
		throw errorEx(tr("Failed to find the key on the token"));
	}
	EVP_PKEY *pkey = p11->getPrivateKey(key, priv_objects[0]);

	if (!pkey) {
		delete p11;
		throw errorEx(tr("Failed to initialize the key on the token"));
	}
	pki_openssl_error();
	return pkey;
}

void pki_scard::changePin()
{
	slotid slot;

	if (!prepare_card(&slot))
		return;

	pkcs11 p11;
	p11.changePin(slot, false);
}

void pki_scard::changeSoPin()
{
	slotid slot;

	if (!prepare_card(&slot))
		return;

	pkcs11 p11;
	p11.changePin(slot, true);
}

void pki_scard::initPin()
{
	slotid slot;

	if (!prepare_card(&slot))
		return;

	pkcs11 p11;
	p11.initPin(slot);
}

int pki_scard::verify()
{
	return true;
}

bool pki_scard::isToken()
{
	return true;
}

QVariant pki_scard::getIcon(dbheader *hd)
{
	return hd->id == HD_internal_name ? QVariant(*icon[0]) : QVariant();
}

bool pki_scard::visible()
{
	QStringList sl;
	if (pki_base::visible())
		return true;

	sl << card_serial << card_manufacturer << card_model <<
		card_label << slot_label << object_id;
	foreach(QString s, sl) {
		if (s.contains(limitPattern))
			return true;
	}
	return false;
}
