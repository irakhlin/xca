TEMPLATE = app
TARGET = xca_db_stat
DEPENDPATH += . lib
INCLUDEPATH += . lib
QT += core
QMAKE_CFLAGS += -static -static-libgcc -static-libstdc++ -lstdc++ -lpthread
QMAKE_LFLAGS += -static -static-libgcc -static-libstdc++ -lstdc++ -lpthread

CONFIG += release

# Input
HEADERS += local.h \
           lib/db.h \
           lib/base.h \
           lib/func.h \
           lib/exception.h

SOURCES += lib/xca_db_stat.cpp \
           lib/db.cpp