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
#include <random>
#include <ctime>
#include <iomanip>

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

void print_timestamp(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm *time_info = localtime(&ts.tv_sec);

    std::cerr << "[" 
              << std::put_time(time_info, "%m/%d/%Y %H:%M:%S")
              << "." << std::setw(3) << std::setfill('0')
              << ts.tv_nsec / 1000000
              << "] ";
}

int main(int argc,char * argv[]){
        if(argc != 6){
        std::cerr << "Usage: " << argv[0] << " <commodity> <price mean> <price standard deviation> <sleep interval> <buffer_size> " << std::endl;
        return 1;
        }

        // Validating arguments
    char *commodity = argv[1];
    double mean = std::atof(argv[2]);
    double stddev = std::atof(argv[3]);
    int sleep_interval = std::atoi(argv[4]);
    int N = std::atoi(argv[5]);

    if(strlen(commodity)>10|| stddev<0|| sleep_interval<=0|| N<=0){
        
        std::cerr << "Invalid arguments." << std::endl;
        return 1;
    }


    // Creating/OPening shared memory
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
    bool size_ready = false;
    for (int i = 0; i < 100; i++) {
        if (fstat(fd, &shm_stat) == -1) {
            perror("fstat");
            close(fd);
            return 1;
        }
        if (static_cast<size_t>(shm_stat.st_size)== size_of_shm) {
           size_ready = true;
           break;
        }

        usleep(10000); 
    }
    if (!size_ready) {
        std::cerr << "Shared memory size is not set correctly." << std::endl;
        close(fd);
        return 1;}


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

        if (shared_data->buffersize != N) {
                std::cerr << "Buffer size mismatch! "
              << "Requested N = " << N
              << ", Existing buffer size = "
              << shared_data->buffersize << std::endl;

                munmap(ptr, size_of_shm);
                return 1;
        }   

        //getting actual buffer address
    Price_Update *buffer =(Price_Update*)((char*)ptr +sizeof(Shared_Data));

    //Random generator
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> distribution(mean,stddev);
    
        //Producer's job
        while(true){
            double price = distribution(gen);
            print_timestamp();
            std::cerr << commodity << ": generating a new value " << price << std::endl;

            sem_wait(&shared_data->empty);

            print_timestamp();
            std::cerr << commodity << ": trying to get mutex on the shared buffer "<< std::endl;
            sem_wait(&shared_data->mutex);

            //Adding price to buffer

            print_timestamp();
            std::cerr << commodity << ": placing " << price << " on the shared buffer "<< std::endl;
            strcpy(buffer[shared_data->in].commodity, commodity);
            buffer[shared_data->in].price = price;
            shared_data->in = (shared_data->in +1) % shared_data->buffersize;

            sem_post(&shared_data->mutex);
            sem_post(&shared_data->full);
            print_timestamp();
            std::cerr << commodity<<": Produced with price "<< price<< " succesfully"<< std::endl;

            print_timestamp();
            std::cerr<< commodity <<": sleeping for "<< sleep_interval << " ms" << std::endl;
            usleep(sleep_interval *1000);
        }


}
