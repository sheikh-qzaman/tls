all: prf https

OPTS = -g -Wall -Wno-pointer-sign -Wunused-variable -Wunused-but-set-variable

prf: prf.c prf.h hmac.c hmac.h md5.c md5.h sha.c sha.h digest.c digest.h hex.c hex.h
	gcc -DTEST_PRF $(INCLUDES) $(OPTS) -o prf prf.c hmac.c md5.c sha.c digest.c hex.c hex.h -lm

SOURCE_FILES=https.c \
tls.c \
prf.c \
x509.c \
base64.c \
hex.c \
des.c \
rc4.c \
aes.c \
huge.c \
rsa.c \
dsa.c \
md5.c \
sha.c \
hmac.c \
digest.c \
asn1.c

HEADER_FILES=tls.h \
x509.h \
base64.h \
hex.h \
des.h \
rc4.h \
aes.h \
huge.h \
rsa.h \
dsa.h \
md5.h \
sha.h \
hmac.h \
digest.h \
asn1.h

https: $(SOURCE_FILES) $(HEADER_FILES)
	gcc $(INCLUDES) $(OPTS) -o https $(SOURCE_FILES) -lm

clean:
	rm -f prf
	rm -f https
	rm -f core
	rm -f core.*
