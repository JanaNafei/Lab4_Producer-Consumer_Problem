# include <iostream>
# include <semaphore.h>
# include <cstring>
# include <unistd.h>
# include <fcntl.h>     // for flags
# include <sys/mman.h>  // for mmap and shm_open
# include <cstdlib>
# include <cerrno>
# include <cstdio>
#include <random>
#include <ctime>
#include <iomanip>
#include <csignal>

volatile sig_atomic_t stop =0;

typedef struct {
    char commodity[11];
    double price;
} Price_Update;

typedef struct {
    int buffersize;
    int in;
    int out;
    int initialized; 
    int active_processes;
    sem_t mutex;
    sem_t empty;
    sem_t full;
} Shared_Data;

void signal_handler(int signal){
    stop=1;
}

bool wait_semaphore(sem_t *sem){
    while(sem_wait(sem)==-1){
        if(errno==EINTR){
            if(stop){
                return false;
            }
            continue;
        }

        perror("sem_wait");
        return false;
    }
     return true;
}

bool wait_semaphore_cleanup(sem_t *sem){
    while(sem_wait(sem)==-1){
        if(errno==EINTR){
            continue;
        }

        perror("sem_wait");
        return false;
    }
     return true;
}

void print_timestamp(){
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm *time_info = localtime(&ts.tv_sec);

    std::cerr << std::put_time(time_info, "%m/%d/%Y %H:%M:%S")<< "." << std::setw(3) << std::setfill('0') << ts.tv_nsec / 1000000;
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

    struct sigaction sa ={};
    sa.sa_handler= signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,&sa,NULL);
    sigaction(SIGTERM,&sa,NULL);

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

        bool done=true;
        shared_data->initialized = 0;
    shared_data->in =0;
    shared_data->out =0;
    shared_data->active_processes=0;
    shared_data->buffersize = N;
    if(sem_init(&shared_data->mutex,1,1)== -1){
        perror("sem_init mutex");
        done=false;
    }
    if(done && sem_init(&shared_data->empty,1,N)== -1){
        perror("sem_init empty");
        done=false;
    }
    if(done && sem_init(&shared_data->full,1,0)== -1){
        perror("sem_init full");
        done= false;
    }
    if(done){
    shared_data->initialized= 1; }
    

        if(!done){
            munmap(ptr, size_of_shm);
            std::cerr << "Failed to initialize shared memory." << std::endl;
            shm_unlink("/my_shm");
            return 1;
        }
      }
      else{
            while(!stop && shared_data->initialized== 0){
                // wait for initialization to complete
                usleep(10000); // sleep for 10ms
            }
        }  
        if(stop){
            munmap(ptr,size_of_shm);
            return 0;
        }

        if(!wait_semaphore(&shared_data->mutex)){
            munmap(ptr,size_of_shm);
            return 1;
        }
        shared_data->active_processes++;
        std::cout << "Registered. Active processes: "<< shared_data->active_processes << std::endl;
        sem_post(&shared_data->mutex);

        //getting actual buffer address
    Price_Update *buffer =(Price_Update*)((char*)ptr +sizeof(Shared_Data));

    //Random generator
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> distribution(mean,stddev);
    
        //Producer's job
        while(!stop){
            double price = distribution(gen);
            print_timestamp();
            std::cerr << " "<<commodity << ": generating a new value " << price << std::endl;

            if(!wait_semaphore(&shared_data->empty)){
            break;
            }

            print_timestamp();
            std::cerr << " "<< commodity << ": trying to get mutex on the shared buffer "<< std::endl;
            if(!wait_semaphore(&shared_data->mutex)){
                sem_post(&shared_data->empty);
                break;
            }

            if(stop){
                sem_post(&shared_data->mutex);
                sem_post(&shared_data->empty);
                break;
            }
            //Adding price to buffer

            print_timestamp();
            std::cerr << " "<< commodity << ": placing " << price << " on the shared buffer "<< std::endl;
            strcpy(buffer[shared_data->in].commodity, commodity);
            buffer[shared_data->in].price = price;
            shared_data->in = (shared_data->in +1) % shared_data->buffersize;

            sem_post(&shared_data->mutex);
            sem_post(&shared_data->full);
            print_timestamp();
            std::cerr << " "<< commodity<<": Produced with price "<< price<< " succesfully"<< std::endl;

            print_timestamp();
            std::cerr<< " "<< commodity <<": sleeping for "<< sleep_interval << " ms" << std::endl;
            usleep(sleep_interval *1000);
        }

        std::cout << "\nProducer is shutting down" << std::endl;

        bool last=false;

    if(wait_semaphore_cleanup(&shared_data->mutex)){ //used a seperate wait function that does not depend on stop
        shared_data->active_processes--;
        std::cout << "Remaining active processes: "<< shared_data->active_processes << std::endl;
        if(shared_data->active_processes == 0){
            last=true;
        }
        sem_post(&shared_data->mutex);
    }
    if(last){
        std::cout <<"\nAll processes exited.Cleaning up..."<< std::endl;
        sem_destroy(&shared_data->mutex);
        sem_destroy(&shared_data->empty);
        sem_destroy(&shared_data->full);
        shm_unlink("/my_shm");
    }
    if(munmap(ptr,size_of_shm)==-1){
        perror("munmap");
        return 0;
    }

    std::cout<< "Producer exited" <<std::endl;
    return 0;
}
