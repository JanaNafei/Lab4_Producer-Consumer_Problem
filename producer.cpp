# include <iostream>
# include <semaphore.h>
# include <cstring>
# include <unistd.h>
# include <fcntl.h>     // for flags
# include <sys/mman.h>  // for mmap and shm_open
# include <sys/stat.h> // for file permissions
# include <cstdlib>
# include <cerrno>
# include <cstdio>

typedef struct {
    char commodity[11];
    double price;
} Price_Update;

typedef struct {
    int buffersize;
    int in;
    int out;
    int initialized; 
    sem_t mutex;
    sem_t empty;
    sem_t full;
} Shared_Data;

bool initialize_shm(Shared_Data * shared_data,int N){
    shared_data->initialized = 0;
    shared_data->in =0;
    shared_data->out =0;
    shared_data->buffersize = N;
    if(sem_init(&shared_data->mutex,1,1)== -1){
        perror("sem_init mutex");
        return false;
    }
    if(sem_init(&shared_data->empty,1,N)== -1){
        perror("sem_init empty");
        return false;
    }
    if(sem_init(&shared_data->full,1,0)== -1){
        perror("sem_init full");
        return false;
    }
    __atomic_store_n(&shared_data->initialized, 1, __ATOMIC_RELEASE); // ensures no producer/consumer will see that initialized is 1 before consumer fifnish initializing
    return true;
}
int main(int argc,char * argv[]){
        if(argc != 6){
        std::cerr << "Usage: " << argv[0] << " <commodity> <price mean> <price standard deviation> <sleep interval> <buffer_size> " << std::endl;
        return 1;
        }

    int N = std::atoi(argv[5]);
    if (N <= 0) {
        std::cerr << "Buffer size must be a positive integer." << std::endl;
        return 1;
    }

        bool is_new =true;
        int fd = shm_open("/my_shm",O_CREAT|O_RDWR|O_EXCL, 0666);
         if (fd == -1) {
        if(errno == EEXIST){
            is_new = false;
            fd = shm_open("/my_shm",O_RDWR, 0666);
            if (fd == -1) {
            perror("shm_open");
            return 1;
        }
    
        }else{
            perror("shm_open");
            return 1;
        }
    } 

    size_t size_of_shm = sizeof(Shared_Data)+ N * sizeof(Price_Update);
     if(is_new){
        if(ftruncate(fd, size_of_shm) == -1) {
            perror("ftruncate");
            return 1;
        }
     }
    struct stat shm_stat;
        if (fstat(fd, &shm_stat) == -1) {
            perror("fstat");
            return 1;
        }
        if (shm_stat.st_size != size_of_shm) {
            std::cerr << "Shared memory size mismatch." << std::endl;
            return 1;
        }
      void *ptr = mmap(NULL,size_of_shm,PROT_READ | PROT_WRITE,MAP_SHARED,fd,0);
      if(ptr == MAP_FAILED){
        perror("mmap");
        close(fd);
        return 1;
      }

      if(close(fd)==-1){
        perror("close"); 
      }

      Shared_Data* shared_data = (Shared_Data*)ptr;
      if (is_new){  
        if(!initialize_shm(shared_data, N)){
            munmap(ptr, size_of_shm);
            std::cerr << "Failed to initialize shared memory." << std::endl;
            return 1;
        }
      }
      else{
            while(__atomic_load_n(&shared_data->initialized, __ATOMIC_ACQUIRE) == 0){
                // wait for initialization to complete
                usleep(10000); // sleep for 10ms
            }
        }

        //getting actual buffer address
    Price_Update *buffer =(Price_Update*)((char*)ptr +sizeof(Shared_Data));


}
