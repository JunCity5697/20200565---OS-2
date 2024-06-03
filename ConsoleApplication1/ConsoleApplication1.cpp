#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <list>
#include <chrono>
#include <string>
#include <sstream>
#include <algorithm>

// 프로세스 구조체
struct Process {
    int id;
    std::string type; // "F" for Foreground, "B" for Background
    bool promoted; // 프로모션 여부
    int waitCount; // WQ에서 기다린 횟수

    Process(int id, std::string type)
        : id(id), type(type), promoted(false), waitCount(0) {}
};

// 동적 큐 노드 구조체
struct Node {
    std::list<Process> processes; // 하나의 프로세스 리스트
    Node* next;                   // 다음 노드를 가리킴

    Node() : next(nullptr) {}
};

// 동적 큐 클래스
class DynamicQueue {
public:
    DynamicQueue() {
        head = new Node();
        tail = head; // 초기 상태에서는 head와 tail이 같은 노드를 가리킴
        promoteNode = head; // 초기 상태에서 promoteNode도 head를 가리킴
        totalProcesses = 0; // 초기 프로세스 개수는 0
        nodeCount = 1; // 초기 노드 개수는 1
    }

    ~DynamicQueue() {
        while (head) {
            Node* temp = head;
            head = head->next;
            delete temp;
        }
    }

    void enqueue(const Process& process, bool performPromote = true) {
        std::lock_guard<std::mutex> lock(queueMutex);
        tail->processes.push_back(process);
        totalProcesses++;

        // 새로운 노드를 생성하고 tail을 업데이트
        if (tail->processes.size() > MAX_PROCESSES_PER_NODE) {
            Node* newNode = new Node();
            tail->next = newNode;
            tail = newNode;
            nodeCount++;
        }

        if (performPromote) {
            promote(); // enqueue 후에 promote를 호출합니다.
        }

        // split_n_merge를 호출합니다.
        split_n_merge(head);
    }

    Process dequeue() {
        std::lock_guard<std::mutex> lock(queueMutex);

        if (head->processes.empty()) {
            throw std::runtime_error("Queue is empty");
        }

        // 첫 번째 프로세스를 가져옵니다.
        Process process = head->processes.front();
        head->processes.pop_front();
        totalProcesses--;

        // 노드가 비어 있으면 노드를 제거합니다.
        if (head->processes.empty() && head->next != nullptr) {
            Node* temp = head;
            head = head->next;
            delete temp;
            nodeCount--;
        }

        promote(); // dequeue 후에 promote를 호출합니다.
        return process;
    }


    bool isEmpty() {
        std::lock_guard<std::mutex> lock(queueMutex);
        return head->processes.empty();
    }

    Process peek() {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (head->processes.empty()) {
            throw std::runtime_error("Queue is empty");
        }
        return head->processes.front();
    }

    void printQueueStatus() {
        std::lock_guard<std::mutex> lock(queueMutex);
        Node* current = head;
        int nodeNumber = 1;
        while (current) {
            std::cout << nodeNumber << " Node : Processes : ";
            if (!current->processes.empty()) {
                for (const auto& proc : current->processes) {
                    std::cout << proc.id << proc.type;
                    if (proc.promoted) std::cout << "*";
                    std::cout << " ";
                }
            }
            if (current == head && current == tail) {
                std::cout << "(bottom/top) ";
            }
            else if (current == head) {
                std::cout << "(bottom) ";
            }
            else if (current == tail) {
                std::cout << "(top) ";
            }
            std::cout << std::endl;
            current = current->next;
            nodeNumber++;
        }
    }

private:
    Node* head;
    Node* tail;
    Node* promoteNode;
    int totalProcesses; // 전체 프로세스 개수
    int nodeCount; // 스택 노드의 수
    std::mutex queueMutex;
    const int MAX_PROCESSES_PER_NODE = 9; // 노드당 최대 프로세스 수

    void promote() {
        if (promoteNode == nullptr) {
            promoteNode = head;
        }

        // promoteNode가 유효하고 프로세스가 있을 때만 실행
        if (promoteNode && !promoteNode->processes.empty()) {
            Process process = promoteNode->processes.front();
            promoteNode->processes.pop_front();
            process.promoted = true;
            tail->processes.push_back(process); // head가 아닌 tail에 추가하여 순서 유지

            // 프로세스를 이동한 후 노드가 비어 있으면 노드를 제거합니다.
            if (promoteNode->processes.empty() && promoteNode->next != nullptr) {
                Node* temp = promoteNode;
                promoteNode = promoteNode->next;
                delete temp;
                nodeCount--;
            }

            // promoteNode를 다음 노드로 이동시킵니다.
            promoteNode = promoteNode->next;
            if (promoteNode == nullptr) {
                promoteNode = head;
            }
        }

        // promote 후 split_n_merge를 호출합니다.
        split_n_merge(head);
    }

    void split_n_merge(Node* node) {
        int threshold = std::max(1, totalProcesses / nodeCount); // 동적 임계치 계산

        while (node != nullptr && node->next != nullptr) {
            if (node->processes.size() > threshold) {
                // 프로세스를 절반으로 나누어 리스트를 만듭니다.
                auto it = node->processes.begin();
                std::advance(it, node->processes.size() / 2);
                std::list<Process> splitList(it, node->processes.end());
                node->processes.erase(it, node->processes.end());

                // 새로운 노드를 생성하고 리스트를 할당합니다.
                Node* newNode = new Node();
                newNode->processes = std::move(splitList);

                // newNode를 다음 노드로 연결합니다.
                newNode->next = node->next;
                node->next = newNode;

                // 상위 노드의 정보를 업데이트합니다.
                nodeCount++;

                // 상위 노드의 길이가 임계치를 넘으면 재귀적으로 split_n_merge를 호출합니다.
                if (newNode->processes.size() > threshold) {
                    split_n_merge(newNode);
                }
            }
            node = node->next;
        }
    }



};

DynamicQueue dq;
std::condition_variable cv;
std::mutex cv_m;
bool exitFlag = false; // 프로그램 종료 플래그

// 대기 큐
std::list<Process> waitQueue;
std::mutex waitQueueMutex;

// PID 공유 변수
int processID = 0;
int schedulerCallCount = 0;

void printQueueStates() {
    std::lock_guard<std::mutex> lock(cv_m);

    // 현재 실행 중인 프로세스
    std::cout << "Running : ";
    if (!dq.isEmpty()) {
        Process runningProcess = dq.peek();
        std::cout << "[" << runningProcess.id << runningProcess.type;
        if (runningProcess.promoted) std::cout << "*";
        std::cout << "]";
    }
    std::cout << std::endl;

    // DQ 상태
    std::cout << "---------------------------------" << std::endl;
    std::cout << "DQ: ";
    dq.printQueueStatus();
    std::cout << "---------------------------------" << std::endl;

    // WQ 상태
    std::cout << "WQ: ";
    for (const auto& process : waitQueue) {
        std::cout << "[" << process.id << process.type;
        if (process.promoted) std::cout << "*";
        std::cout << ":" << 5 - process.waitCount << "] ";
    }
    std::cout << std::endl;
}

void updateWaitQueue() {
    std::lock_guard<std::mutex> lock(waitQueueMutex);

    auto it = waitQueue.begin();

    while (it != waitQueue.end()) {
        it->waitCount++;
        if (it->waitCount >= 5) {
            dq.enqueue(*it);
            it = waitQueue.erase(it);
        }
        else {
            ++it;
        }
    }
}

// Shell 스레드 함수
void shell(int Y) {
    std::string command;
    while (!exitFlag) {
        std::cout << "shell> ";
        std::getline(std::cin, command);

        if (command.empty()) {
            continue;
        }

        std::istringstream iss(command);
        std::string cmd;
        iss >> cmd;

        if (cmd == "print") {
            int repeat;
            iss >> repeat;
            for (int i = 0; i < repeat; ++i) {
                std::cout << "Printing from shell: " << i + 1 << std::endl;
            }
        }
        else if (cmd == "enqueue") {
            std::string type;
            iss >> type;
            if (type == "fg") {
                std::lock_guard<std::mutex> lock(cv_m);
                Process fgProcess = { processID++, "F" };
                std::lock_guard<std::mutex> waitLock(waitQueueMutex);
                waitQueue.push_back(fgProcess);
                cv.notify_all();
            }
            else if (type == "bg") {
                std::lock_guard<std::mutex> lock(cv_m);
                Process bgProcess = { processID++, "B" };

                dq.enqueue(bgProcess);

                cv.notify_all();
            }
        }
        else if (cmd == "dequeue") {
            try {
                Process process = dq.dequeue();
                std::cout << "Dispatched Process ID: " << process.id << process.type << std::endl;

                // 예시로 FG 프로세스를 WQ에 추가
                if (process.type == "F") {
                    std::lock_guard<std::mutex> lock(waitQueueMutex);
                    process.waitCount = 0;
                    waitQueue.push_back(process);
                }
            }
            catch (const std::runtime_error& e) {
                std::cout << e.what() << std::endl;
            }
        }
        else if (cmd == "exit") {
            exitFlag = true;
        }
        else {
            std::cout << "Unknown command" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(Y));
    }
}


// Monitor 스레드 함수
void monitor(int X) {
    while (!exitFlag) {
        {
            std::lock_guard<std::mutex> lock(cv_m);
            Process bgProcess = { processID++, "B" };
            dq.enqueue(bgProcess, false); // promote를 호출하지 않음
        }
        cv.notify_all();

        updateWaitQueue(); // WQ를 업데이트
        schedulerCallCount++;

        // 큐의 상태 출력
        printQueueStates();
        std::this_thread::sleep_for(std::chrono::seconds(X));
    }
}

int main() {
    int X = 4; // 모니터 출력 주기 (초)
    int Y = 2; // 쉘 명령 후 대기 시간 (초)

    std::thread shellThread(shell, Y);
    std::thread monitorThread(monitor, X);

    shellThread.join();
    monitorThread.join();

    return 0;
}
