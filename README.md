# XCA -- X Certificate and Key Management

## __Additional Notes__

 * This is a copy of the repository found here https://sourceforge.net/p/xca/code all original work was done by the creators
 * This repository is to track some minor updates and changes that would allow for use of openssl 1.1.0 and fully build with QT5 as it seems original work has stoped
 * Feel free to submit pull requests, fixes and bug here on github, I will do my best to look though them just keep in mind I am not the original author
## __Release Notes__

 * Make a backup copy of your database!

## __Changelog:__

### xca 1.3.3

 * Migrated to github branch to continue updated: https://github.com/irakhlin/xca
 * Windows: Libtool library libltdl updated to 2.4.6
 * Windows: updated to Openssl 1.1.0f
 * Windows: Modifications to base path detection to conform and build with unicode
 * Windows: built using mingw64 toolchain for 64bit support and minor modifications for headers to work correctly
 * Windows: Update to use QT5.8-static (mingw-w64-x86_64-qt5-static) from msys2 package
 * Windows: Build release version as full static binary for both xca.exe and xca_db_stat.exe

### xca 1.3.2


 * Gentoo Bug #562288 linking fails
 * Add OID resolver, move some Menu items to "Extra"
 * SF. Bug. #81 Make xca qt5 compatible
 * SF. Bug. #107 error:0D0680A8:asn1 encoding
 * Don't validate notBefore and notAfter if they are disabled.


### xca 1.3.1


 * Fix endless loop while searching for a signer of a CRL


### xca 1.3.0


 * Update to OpenSSL 1.0.2d for Windows and MAC
 * SF Bug #105 1.2.0 OS X Retina Display Support
 * Digitaly sign Windows and MAC binaries with a valid certificate
 * Refactor the context menu. Exporting many selected items the clipboard or a PEM file now works. Certificate renewal and revocation now be performed on a batch of certificates.
 * Feat. Reg. #83 Option to revoke old certificate when renewing
 * Refactor revocation handling. All revocation information is with the CA and may be modified. certificates may now be deleted from the database
 * Support nameConstraints, policyMappings, InhibitAnyPolicy, PolicyConstraint (OSCP)noCheck when transforming certificates to templates or OpenSSL configs
 * Fix SF Bug #104 Export to template introduces spaces
 * Add option for disabling legacy Netscape extensions
 * Support exporting SSH2 public key to the clipboard
 * SF Bug #102 Weak entropy source used for key generation: /dev/random, mouse/kbd entropy, token RNG
 * SF Feat. Req. #80 Create new certificate, on existing certificate, same for requests
 * Add Cert/Req Column for Signature Algorithm
 * SF Feat. Req. #81 Show key size in New Certificate dialog
 * Distinguish export from transform: Export writes to an external file, Transform generates another XCA item

