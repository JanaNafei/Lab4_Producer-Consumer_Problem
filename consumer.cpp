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

#define NUM_COMMODITIES 11

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
    __atomic_store_n(&shared_data->initialized, 1, __ATOMIC_RELEASE); // ensures no producer will see that initialized is 1 before consumer fifnish initializing
    return true;
}

const char* commodities[NUM_COMMODITIES] = {
    "ALUMINIUM",
    "COPPER",
    "COTTON",
    "CRUDEOIL",
    "GOLD",
    "LEAD",
    "MENTHAOIL",
    "NATURALGAS",
    "NICKEL",
    "SILVER",
    "ZINC"

};

void display_dashboard(double current_prices[],double prices_history[][5], int history_count[], const char* arrow_prices[], const char* arrow_avg[],bool avg_initial[],double previous_avg[]){
    printf("\e[1;1H\e[2J");

    printf("+------------------+------------+-------------+\n");
    printf("| %-16s | %10s | %9s |\n",
           "Currency", "Price", "AvgPrice");
    printf("+------------------+------------+-------------+\n");

    for(int i = 0; i < NUM_COMMODITIES; i++){
        double sum =0.0;
        for(int j=0; j<history_count[i];j++){
            sum += prices_history[i][j];
        }

        double avg = 0.0;
        if(history_count[i] >0){
            avg = sum /history_count[i];
        }

        if(history_count[i]>0){ 
            if(!avg_initial[i]){
                        arrow_avg[i] = "";
                        avg_initial[i] = true;
                    }else if(avg > previous_avg[i]){
                        arrow_avg[i] = "↑";
                    }else if(avg < previous_avg[i]){
                        arrow_avg[i] = "↓";
                    }else{
                        arrow_avg[i] = "=";
                    }
                    previous_avg[i] = avg;
        }



        const char* price_color = "\033[0m";
        const char* avg_color = "\033[0m";

        if(strcmp(arrow_prices[i], "↑") == 0){
            price_color ="\033[32m"; 

        }else if(strcmp(arrow_prices[i], "↓") == 0){
            price_color ="\033[31m";
        }else if(strcmp(arrow_prices[i], "=") == 0){
            price_color ="\033[33m";
        }

        if(strcmp(arrow_avg[i], "↑") == 0){
            avg_color ="\033[32m"; 

        }else if(strcmp(arrow_avg[i], "↓") == 0){
            avg_color ="\033[31m";
        }else if(strcmp(arrow_avg[i], "=") == 0){
            avg_color ="\033[33m";
        }


        printf("| %-16s | %s%7.2lf %-2s\033[0m | %s%7.2lf %-2s\033[0m |\n",
       commodities[i],
       price_color,
       current_prices[i],
       arrow_prices[i],
       avg_color,
       avg,
       arrow_avg[i]);
    }

    printf("+------------------+------------+-------------+\n");

    fflush(stdout);

}

int main(int argc, char *argv[]){
    if(argc != 2){
        std::cerr << "Usage: " << argv[0] << " <buffer_size>" << std::endl;
        return 1;
    }
    int N = std::atoi(argv[1]);
    if (N <= 0) {
        std::cerr << "Buffer size must be a positive integer." << std::endl;
        return 1;
    }

    bool is_new =true;
    int fd = shm_open("/my_shm",O_CREAT | O_RDWR | O_EXCL, 0666);
    if (fd == -1) {
        if(errno == EEXIST){
            is_new = false;
            fd = shm_open("/my_shm",O_RDWR, 0666);
            
            if (fd == -1) {
            perror("shm_open");
            return 1;}

        }else{
        std::cerr << "Failed to create shared memory." << std::endl;
        return 1;
        }
    }
    size_t size_of_shm = sizeof(Shared_Data)+ N *sizeof(Price_Update);

    if(is_new){
    if(ftruncate(fd,size_of_shm) == -1){
        std::cerr << "failed to set size of shared memory." << std::endl;
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
        if (static_cast<size_t>(shm_stat.st_size) == size_of_shm) {
           size_ready = true;
           break;
        }

        usleep(10000); 
    }
    if (!size_ready) {
        std::cerr << "Shared memory size is not set correctly." << std::endl;
        close(fd);
        return 1;}

    void *ptr = mmap(NULL, size_of_shm, PROT_READ | PROT_WRITE ,MAP_SHARED,fd,0);
    if(ptr == MAP_FAILED){
        std::cerr << "Failed to map shared memory." << std::endl;
        close(fd);
        return 1;
    }
    if (close(fd) == -1) {
    perror("close");
    }
    Shared_Data *shared_data = (Shared_Data *)ptr;

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
    
    //Consumer's job

    // helper arrays
    double current_prices[NUM_COMMODITIES] = {0}; // -> tracks current price of evey commodity
    double prices_history[NUM_COMMODITIES][5] ={0}; // -> (2D) previous 5 prices of every commodity (including the current price)
    int history_count[NUM_COMMODITIES] = {0}; // -> tracks the number of price changes recieved for each
    int history_next[NUM_COMMODITIES] = {0}; // -> next index to write on the prices_history array for this commodity (circular buffer)
    // For arrows
    const char* arrow_prices[NUM_COMMODITIES];
    bool price_initial[NUM_COMMODITIES] = {false};

    const char* arrow_avg[NUM_COMMODITIES];
    bool avg_initial[NUM_COMMODITIES] = {false};
    double previous_avg[NUM_COMMODITIES] = {0};

    for(int i = 0; i < NUM_COMMODITIES; i++){
    arrow_prices[i] = "";
    arrow_avg[i] = "";
    }

    while(true){
        sem_wait(&shared_data->full);
        sem_wait(&shared_data->mutex);

        // REad from buffer
        Price_Update item = buffer[shared_data->out];
        shared_data->out = (shared_data->out +1) % shared_data->buffersize;

        sem_post(&shared_data->mutex);
        sem_post(&shared_data->empty);

        for(int i=0; i<NUM_COMMODITIES; i++){
            if(strcmp(item.commodity, commodities[i])==0){
                
                if(!price_initial[i]){
                    arrow_prices[i]="";
                    price_initial[i]=true;
                }else if(item.price > current_prices[i]){
                    arrow_prices[i] = "↑";
                }else if(item.price < current_prices[i]){
                    arrow_prices[i] = "↓";
                }else{
                    arrow_prices[i] = "=";
                }

                current_prices[i] = item.price;

                prices_history[i][history_next[i]] = item.price;
                history_next[i] = (history_next[i] + 1) % 5;
                if(history_count[i] < 5){
                    history_count[i]++;
                }
                    
                break;
            }
        }
        
        display_dashboard(current_prices,prices_history,history_count,arrow_prices,arrow_avg,avg_initial,previous_avg);

    }

}