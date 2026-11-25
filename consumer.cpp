# include <semaphore.h>

typedef struct {
    char commodity[11];
    double price;
} Price_Update;

typedef struct {
    int buffersize;
    int in;
    int out;
    sem_t mutex;
    sem_t empty;
    sem_t full;
} Shared_Data;