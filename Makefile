
CFLAGS=-O2

OFILES=\
	./lib/httpd.o\
	./lib/utils.o\
	./lib/plist.o\
	./lib/alac/alac.o\
	./lib/ed25519/keypair.o\
	./lib/ed25519/key_exchange.o\
	./lib/ed25519/add_scalar.o\
	./lib/ed25519/ge.o\
	./lib/ed25519/sc.o\
	./lib/ed25519/seed.o\
	./lib/ed25519/fe.o\
	./lib/ed25519/sha512.o\
	./lib/ed25519/sign.o\
	./lib/ed25519/verify.o\
	./lib/http_response.o\
	./lib/base64.o\
	./lib/aes_ctr.o\
	./lib/raop.o\
	./lib/rsakey.o\
	./lib/logger.o\
	./lib/fairplay_playfair.o\
	./lib/http_request.o\
	./lib/dnssd.o\
	./lib/pairing.o\
	./lib/playfair/omg_hax.o\
	./lib/playfair/playfair.o\
	./lib/playfair/sap_hash.o\
	./lib/playfair/modified_md5.o\
	./lib/playfair/hand_garble.o\
	./lib/raop_buffer.o\
	./lib/raop_rtp.o\
	./lib/http_parser.o\
	./lib/netutils.o\
	./lib/rsapem.o\
	./lib/sdp.o\
	./lib/crypto/rc4.o\
	./lib/crypto/aes.o\
	./lib/crypto/hmac.o\
	./lib/crypto/sha1.o\
	./lib/crypto/bigint.o\
	./lib/crypto/md5.o\
	./lib/digest.o\
	./lib/curve25519/curve25519-donna-c64.o\

all: shairplay

lib/%.o: lib/%.c
	$(CC) $(CFLAGS) -Iinclude/shairplay -c -o $@ $<

libshairplay.a: $(OFILES)
	$(AR) r $@ $(OFILES)

%.o: %.c
	$(CC) $(CFLAGS) -Iinclude -c $<

shairplay: shairplay.o libshairplay.a
	$(CC) -o $@ shairplay.o libshairplay.a -lm -lao -ldns_sd

clean:
	rm -f shairplay shairplay.o libshairplay.a $(OFILES)

