#ifndef __WORKER_PROTOCOL_H
#define __WORKER_PROTOCOL_H


#define WORKER_MAGIC_1 0x6e697864
#define WORKER_MAGIC_2 0x6478696e


typedef enum {
    wopQuit,
    wopIsValidPath,
    wopQuerySubstitutes,
    wopAddToStore,
    wopAddTextToStore,
} WorkerOp;


#endif /* !__WORKER_PROTOCOL_H */
