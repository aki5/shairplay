
CFLAGS=-g

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
	$(CC) $(CFLAGS) -o $@ shairplay.o libshairplay.a -lm -lasound -ldns_sd

clean:
	rm -f shairplay shairplay.o libshairplay.a $(OFILES)

HFILES=\
	 ./include/shairplay/dnssd.h \
	 ./include/shairplay/raop.h \
	 ./lib/utils.h \
	 ./lib/fairplay.h \
	 ./lib/rsapem.h \
	 ./lib/raop_rtp.h \
	 ./lib/http_request.h \
	 ./lib/sdp.h \
	 ./lib/global.h \
	 ./lib/raop_handlers.h \
	 ./lib/pairing.h \
	 ./lib/alac/alac.h \
	 ./lib/alac/stdint_win.h \
	 ./lib/plist.h \
	 ./lib/compat.h \
	 ./lib/ed25519/sc.h \
	 ./lib/ed25519/ed25519.h \
	 ./lib/ed25519/ge.h \
	 ./lib/ed25519/precomp_data.h \
	 ./lib/ed25519/fixedint.h \
	 ./lib/ed25519/sha512.h \
	 ./lib/ed25519/fe.h \
	 ./lib/raop_buffer.h \
	 ./lib/aes_ctr.h \
	 ./lib/playfair/omg_hax.h \
	 ./lib/playfair/playfair.h \
	 ./lib/base64.h \
	 ./lib/sockets.h \
	 ./lib/httpd.h \
	 ./lib/digest.h \
	 ./lib/memalign.h \
	 ./lib/logger.h \
	 ./lib/threads.h \
	 ./lib/crypto/bigint_impl.h \
	 ./lib/crypto/os_port.h \
	 ./lib/crypto/config.h \
	 ./lib/crypto/bigint.h \
	 ./lib/crypto/crypto.h \
	 ./lib/http_parser.h \
	 ./lib/rsakey.h \
	 ./lib/dnssdint.h \
	 ./lib/netutils.h \
	 ./lib/http_response.h \
	 ./lib/curve25519/curve25519.h \

$(OFILES): $(HFILES)

