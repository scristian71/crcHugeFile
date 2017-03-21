#include <iostream>
#include <chrono>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <atomic>
#include <algorithm>
#include <fstream>

#define MAX_INPUT_ITEMS_IN_QUEUE 5000
#define TOTAL_INPUT_ITEMS 1024*1024*1

typedef uint64_t CheckSumType;
typedef std::function<bool(void)> ExitCondition;

static std::atomic<bool> running(true);
static std::atomic<uint32_t> finished(0);
static uint64_t counter(1);

struct InputData
{
    char* addr;
    uint64_t position;
    InputData(): addr(nullptr), position(0){}
    InputData(char* _addr, uint64_t _position): addr(_addr), position(_position){}
    InputData(const InputData& other): addr(other.addr), position(other.position){}
    InputData(InputData&& other):addr(other.addr), position(other.position){}
    InputData& operator=(const InputData& other){addr = other.addr; position = other.position;}
    InputData& operator=(InputData&& other){addr = other.addr; position = other.position;}
};

struct OutputData
{
    CheckSumType crc;
    uint64_t position;
    OutputData(): crc(), position(0){}
    OutputData(CheckSumType _crc, uint64_t _position): crc(_crc), position(_position){}
    OutputData(const OutputData& other): crc(other.crc), position(other.position){}
    OutputData(OutputData&& other): crc(other.crc), position(other.position){}
    OutputData& operator=(const OutputData& other) {crc = other.crc; position = other.position;}
    OutputData& operator=(OutputData&& other) {crc = other.crc; position = other.position;}
};

template <typename T>
class ConcurentQueue
{
public:
    ConcurentQueue(uint64_t _max_capacity): max_capacity(_max_capacity){}
    //blocking function - will wait until there is a new item in queue
    T pop(ExitCondition exitCondition) {
        std::unique_lock<std::mutex> lk(m);
        while ((data.size() == 0) && !exitCondition())
            cv.wait(lk);

        T retVal;
        if (data.size() > 0)
        {
            retVal = std::move(data.front());
            data.pop();
        }
        cvi.notify_one();
        return retVal;
    }
    //blocking function - will wait untill new item can be added to the queue
    bool push(const T& item, ExitCondition exitCondition) {
        std::unique_lock<std::mutex> lk(m);
        while ((data.size() >= max_capacity) && !exitCondition())
            cvi.wait(lk);

        if (data.size() < max_capacity) {
            data.push(item);
            cv.notify_one();
            return true;
        }
        return false;
    }
    size_t size() {
        std::unique_lock<std::mutex> lk(m);
        return data.size();
    }
    void exit_waiting_status() {
        std::unique_lock<std::mutex> lk(m);
        cv.notify_all();
        cvi.notify_all();
    }

private:
    std::mutex m;
    std::condition_variable_any cv;
    std::condition_variable_any cvi;
    std::queue<T> data;
    uint64_t max_capacity;
};

CheckSumType DuffyDuck(char* position)
{
//    std::this_thread::sleep_for(std::chrono::nanoseconds(2));
    std::this_thread::yield();
    return reinterpret_cast<CheckSumType>(position);
}

static ConcurentQueue<InputData> inputQueue(MAX_INPUT_ITEMS_IN_QUEUE);
static ConcurentQueue<OutputData> outputQueue(MAX_INPUT_ITEMS_IN_QUEUE);

void workerMain()
{
    int items = 0;
    while (running.load() || inputQueue.size() > 0)
    {
        InputData data = inputQueue.pop([](){return !running.load();});
        if (data.position > 0) { //data is valid
            CheckSumType crc = DuffyDuck(data.addr);
            OutputData dataOut(crc, data.position);
            outputQueue.push(dataOut, [](){return false;});
            items++;
        }
    }
    std::cout << "Worker items:"<< items << std::endl;
    finished++;
}

void producerMain(char* startAddr)
{
    while (counter < TOTAL_INPUT_ITEMS)
    {
        InputData data(startAddr, counter);
        inputQueue.push(data, [](){return false;});
        startAddr += 1024;
        counter++;
    }
}

void consumerMain(std::ostream& os, ExitCondition exitCondition)
{
    uint64_t currentCounter = 1;

    auto cmp = [](const OutputData& left, const OutputData& right) { return left.position > right.position;};
    std::priority_queue<OutputData, std::vector<OutputData>, decltype(cmp)> received(cmp);
    while (running.load() || outputQueue.size() > 0 || !exitCondition())
    {
        OutputData data = outputQueue.pop([](){return !running.load();});
        if (data.position > 0) //data is valid
            received.push(data);
        while (received.top().position == currentCounter)
        {
            os << received.top().crc << std::endl;
            received.pop();
            currentCounter++;
        }
    }

    std::cout << "LocalQueueSize:" << received.size() << ":currcounter:" << currentCounter << std::endl;
    while (received.size()  > 0)
    {
        std::cout << "IteminQueue:" << received.top().position << std::endl;
        received.pop();
    }
}

int main(int argc, char *argv[])
{
    char dummy;
    uint32_t hwconcurr = std::thread::hardware_concurrency();

    std::ofstream iusrfile;
    iusrfile.open("out.txt");
    std::vector<std::thread> pool;
    std::thread producer(producerMain, &dummy);
    for (uint32_t i = 0; i < hwconcurr; i++)
        pool.emplace_back(workerMain);
    std::thread consumer(consumerMain, std::ref(iusrfile), [hwconcurr](){return finished.load() == hwconcurr;});

    producer.join();
    running.store(false);

    inputQueue.exit_waiting_status();
    outputQueue.exit_waiting_status();
    for (uint32_t i = 0; i < pool.size(); i++)
        pool[i].join();

    consumer.join();

    std::cout << "InputQueueSize:" << inputQueue.size() << std::endl;
    std::cout << "OutputQueueSize:" << outputQueue.size() << std::endl;
    iusrfile.close();
    return 0;
}
