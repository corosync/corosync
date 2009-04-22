#ifndef CRYPTO_H_DEFINED
#define CRYPTO_H_DEFINED

#include <stdint.h>

#define DIGEST_SHA1     0
#define PRNG_SOBER      0


enum {
   CRYPT_OK=0,             /* Result OK */
   CRYPT_ERROR,            /* Generic Error */
   CRYPT_NOP,              /* Not a failure but no operation was performed */

   CRYPT_INVALID_KEYSIZE,  /* Invalid key size given */
   CRYPT_INVALID_ROUNDS,   /* Invalid number of rounds */
   CRYPT_FAIL_TESTVECTOR,  /* Algorithm failed test vectors */

   CRYPT_BUFFER_OVERFLOW,  /* Not enough space for output */
   CRYPT_INVALID_PACKET,   /* Invalid input packet given */

   CRYPT_INVALID_PRNGSIZE, /* Invalid number of bits for a PRNG */
   CRYPT_ERROR_READPRNG,   /* Could not read enough from PRNG */

   CRYPT_INVALID_CIPHER,   /* Invalid cipher specified */
   CRYPT_INVALID_HASH,     /* Invalid hash specified */
   CRYPT_INVALID_PRNG,     /* Invalid PRNG specified */

   CRYPT_MEM,              /* Out of memory */
   CRYPT_PK_TYPE_MISMATCH, /* Not equivalent types of PK keys */
   CRYPT_PK_NOT_PRIVATE,   /* Requires a private PK key */

   CRYPT_INVALID_ARG,      /* Generic invalid argument */
   CRYPT_FILE_NOTFOUND,    /* File Not Found */

   CRYPT_PK_INVALID_TYPE,  /* Invalid type of PK key */
   CRYPT_PK_INVALID_SYSTEM,/* Invalid PK system specified */
   CRYPT_PK_DUP,           /* Duplicate key already in key ring */
   CRYPT_PK_NOT_FOUND,     /* Key not found in keyring */
   CRYPT_PK_INVALID_SIZE,  /* Invalid size input for PK parameters */

   CRYPT_INVALID_PRIME_SIZE/* Invalid size of prime requested */
};

struct sha1_state {
    unsigned long long length;
    unsigned long state[5], curlen;
    unsigned char buf[64];
};
typedef union Hash_state {
    struct sha1_state   sha1;
} hash_state;

struct _hash_descriptor {
    const char *name;
    unsigned char ID;
    unsigned long hashsize;       /* digest output size in bytes  */
    unsigned long blocksize;      /* the block size the hash uses */
    unsigned char DER[64];        /* DER encoded identifier */
    unsigned long DERlen;         /* length of DER encoding */
    void (*init)(hash_state *);
    int (*process)(hash_state *, const unsigned char *, unsigned long);
    int (*done)(hash_state *, unsigned char *);
    int  (*test)(void);
};

extern const struct _hash_descriptor *hash_descriptor[];

void sha1_init(hash_state * md);
int sha1_process(hash_state * md, const unsigned char *buf, unsigned long len);
int sha1_done(hash_state * md, unsigned char *hash);
int sha1_test(void);

int hash_memory(int hash, const unsigned char *data, unsigned long len, unsigned char *dst, unsigned long *outlen);

#define MAXBLOCKSIZE 128
typedef struct Hmac_state {
     hash_state     md;
     int            hash;
     hash_state     hashstate;
     unsigned char  key[MAXBLOCKSIZE];
} hmac_state;

int hmac_init(hmac_state *hmac, int hash, const unsigned char *key, unsigned long keylen);
int hmac_process(hmac_state *hmac, const unsigned char *buf, unsigned long len);
int hmac_done(hmac_state *hmac, unsigned char *hashOut, unsigned long *outlen);
int hmac_test(void);
int hmac_memory(int hash, const unsigned char *key, unsigned long keylen,
                       const unsigned char *data, unsigned long len,
                       unsigned char *dst, unsigned long *dstlen);

struct sober128_prng {
    uint32_t      R[17],          /* Working storage for the shift register */
                 initR[17],      /* saved register contents */
                 konst,          /* key dependent constant */
                 sbuf;           /* partial word encryption buffer */

    int          nbuf,           /* number of part-word stream bits buffered */
                 flag,           /* first add_entropy call or not? */
                 set;            /* did we call add_entropy to set key? */

};

typedef union Prng_state {
    struct sober128_prng sober128;
} prng_state;

struct _prng_descriptor {
    const char *name;
    int  export_size;    /* size in bytes of exported state */
    int (*start)(prng_state *);
    int (*add_entropy)(const unsigned char *, unsigned long, prng_state *);
    int (*ready)(prng_state *);
    unsigned long (*read)(unsigned char *, unsigned long, prng_state *);
};

extern const struct _prng_descriptor *prng_descriptor[];

int sober128_start(prng_state *prng);
int sober128_add_entropy(const unsigned char *buf, unsigned long len, prng_state *prng);
int sober128_ready(prng_state *prng);
unsigned long sober128_read(unsigned char *buf, unsigned long len, prng_state *prng);
int sober128_done(prng_state *prng);
int sober128_export(unsigned char *out, unsigned long *outlen, prng_state *prng);
int sober128_import(const unsigned char *in, unsigned long inlen, prng_state *prng);
int sober128_test(void);

unsigned long rng_get_bytes(unsigned char *buf,
                                   unsigned long len,
                                   void (*callback)(void));

int rng_make_prng(int bits, int wprng, prng_state *prng, void (*callback)(void));

#endif /* CRYPTO_H_DEFINED */
