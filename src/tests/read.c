#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "../xchacha/src/xchacha.h"
#include "../siphash/src/siphash.h"

//                                0123456789abcdef0123456789abcdef
uint8_t my_encryption_key[32] = {"Do not use this encryption key!"};
uint8_t my_signature_key[16] =  {"Or this key..."};

#define FILENAME "C:/Users/Brad/Documents/GitHub/hermes_CodeBlocks/HERMES/bin/Release/demofile.bin"

// Decode a file

xChaCha_ctx cCtx;   // encryption context
siphash_ctx hCtx;   // HMAC context
uint64_t hctr;      // HMAC counter
FILE* file;         // input file
uint8_t HMAC[16];   // captured HMAC

void SkipEndTags(void) {
    int c;
    do {
        c = fgetc(file);
        if (c == EOF) return;
    } while (c == 0x12);
}

int NextChar(void) {
    int c = fgetc(file);
    if (c < 0) return c;                        // EOF
    if (c == 0x10) {
        c = fgetc(file);
        if (c > 3) switch(c) {
        case 4:
            sip_hmac_final(&hCtx, HMAC);
            hctr++;
            return 0x100;
        default:
            printf("\nUnknown 10 ??");
        } else c += 0x10;
    }
    sip_hmac_putc(&hCtx, c);
    return c;
}

int NextBlock(uint8_t *dest) {                  // return bytes read before HMAC
    for (int i = 0; i < 16; i++) {
        int c = NextChar();
        if (c < 0) return c;
        if (c & 0x100) return i;                // HMAC tag
        *dest++ = c;
    }
    return 16;
}

int TestHMAC(uint8_t *src) {
    for (int i = 0; i < 16; i++) if (src[i] != HMAC[i]) return 1;
    return 0;
}

void dump(const uint8_t *src, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if ((i % 33) == 0) printf("\n___");
        printf("%02X ", src[i]);
    }
    printf("<- ");
}

int main() {
    uint8_t IV[17];
    printf("\nReading demofile.bin ");
    file = fopen(FILENAME, "rb");
    if (file == NULL) {
        printf("\nError opening file!");
        return 1;
    }
    int c;
    do {                                        // skip boilerplate
        c = fgetc(file);
        if (c == EOF) goto end;
    } while (c != 0x12);
    fgetc(file);                                // skip extra `12`
    printf("\nBegin CHALLENGE message at 0x%X ", (unsigned int)ftell(file));
    hctr = 0;
    sip_hmac_init(&hCtx, my_signature_key, 16, hctr);
    if (NextChar() != 23) {
        printf("\nError: Couldn't find challenge tag");
        goto end;
    }
    NextBlock(IV);
    memcpy(&hctr, IV, 8);                       // initial IV and hctr
    dump((uint8_t*)&hctr, 8); printf("Initial 64-bit HMAC counter");
    dump(IV, 16); printf("mIV (visible, but used once to encrypt cIV)");
    xc_crypt_init(&cCtx, my_encryption_key, IV);// set up to decrypt cIV
    NextBlock(IV);
    NextChar();                                 // ignore 'avail' field
    NextChar();
    xc_crypt_block(&cCtx, IV, IV, 1);           // mIV --> cIV
    xc_crypt_init(&cCtx, my_encryption_key, IV);
    dump(IV, 16); printf("cIV (private)");
    if (NextBlock(IV)) {
        printf("\nError: Expected 10 04 HMAC trigger");
        goto end;
    }
    NextBlock(IV);                              // expected HMAC
    if (NextChar() != 0x12) {
        printf("\nError: Expected END tag");
        goto end;
    }
    if (TestHMAC(IV)) {
        printf("\nError: Bad HMAC");
        dump(IV, 16); printf("expected");
        dump(HMAC, 16); printf("actual");
        goto end;
    }
    fgetc(file);                                // skip extra `12`
    printf("\nDecryption stream has been initialized, fp=0x%X ", (unsigned int)ftell(file));
// Begin RAW PACKET messages
    while(1) {
        sip_hmac_init(&hCtx, my_signature_key, 16, hctr);
        int c = NextChar();
        if (c < 0) {
            printf("\nFINISHED!");
            goto end;
        }
        if (c != 31) {
            printf("\nError: Expected RAW PACKET tag");
            goto end;
        }
        while (1) {
            int n = NextBlock(IV);
            if (n < 0) {
                printf("\nError: Early EOF %d", n);
                goto end;
            }
            if (n < 16) break;
            xc_crypt_block(&cCtx, IV, IV, 1);
            IV[16] = 0;
            dump(IV, n); printf("%s", IV);
        }
        NextBlock(IV);                          // expected HMAC
        if (TestHMAC(IV)) {
            printf("\nError: Bad HMAC");
            dump(IV, 16); printf("expected");
            dump(HMAC, 16); printf("actual");
            goto end;
        }
        fgetc(file);
        fgetc(file);
    }


end:
    fclose(file);
}
