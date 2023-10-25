#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 10
#define PORT 8080
#define MAX_MESSAGE_LEN 128
struct connection *head = NULL;
struct connection *cur = NULL;

// 클라이언트 정보 구조체
struct client_socket
{
    int socket;
    char *name;
    char *addr;
    int port;
    char buffer[MAX_MESSAGE_LEN];
};

struct connection
{
    struct client_socket *client; // 클라이언트 소켓 및 연결 정보
    struct connection *next;      // 연결 리스트 데이터
    unsigned int is_alive;        // 연결 상태
};

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct connection *create_conenction(int client_fd, struct sockaddr_in addr)
{
    /* 신규 연결 구조체를 생성한다. */
    struct client_socket *client = (struct client_socket *)malloc(sizeof(struct client_socket));
    client->socket = client_fd;
    client->addr = inet_ntoa(addr.sin_addr); // 문자열 복사
    client->port = ntohs(addr.sin_port);     // 문자열 복사

    struct connection *new_connection = (struct connection *)malloc(sizeof(struct connection));
    new_connection->client = client; // 클라이언트 구조체의 정보 저장
    new_connection->is_alive = 1;    // connection is alive
    new_connection->next = NULL;
    return new_connection;
}

int free_connection(struct connection *close_connection)
{
    /*연결 구조체를 삭제하는 작업을 수행*/
    printf("Server Removed connection: %s:%d \n", close_connection->client->addr, close_connection->client->port);
    // pthread_mutex_lock(&mutex);
    close(close_connection->client->socket); // 소켓 닫음
    free(close_connection->client);          // 클라이언트 정보 제거
    free(close_connection);                  // 커넥션 자원할당 제거
    // pthread_mutex_unlock(&mutex);
    return 0;
}

int close_socket_connection()
{
    /*종료된 연결에서 자원을 회수한다.*/
    struct connection *cur = head;
    struct connection *before = head;
    // 단일 커넥션인 경우
    if (cur->next == NULL)
    {
        if (cur->is_alive == 0) // 첫 커넥션을 삭제하는 경우
        {
            pthread_mutex_lock(&mutex);
            free_connection(cur);
            head = NULL;
            pthread_mutex_unlock(&mutex);
            return 0;
        }
    }
    // 다중 커넥션인 경우
    while (cur != NULL)
    {
        if (cur->is_alive == 0) // 내 소켓이 아니고 연결이 살아있으면
        {
            pthread_mutex_lock(&mutex);
            before->next = cur->next; // 삭제 진행
            if (cur == head)
                head = cur->next;
            free_connection(cur);
            pthread_mutex_unlock(&mutex);
            break;
        }
        before = cur;
        cur = cur->next;
    }
    return 0;
}

void send_msg(int my_socket, char *addr, char *msg)
{
    struct connection *cur = head; // 커넥션 연결 리스트의 헤드
    while (cur != NULL)
    {
        pthread_mutex_lock(&mutex);
        struct client_socket *now_client = cur->client;               // cur이 도중에 삭제되면 안된다.
        if ((now_client->socket != my_socket) && (cur->is_alive > 0)) // 내 소켓이 아니고 연결이 살아있으면
        {
            write(cur->client->socket, msg, MAX_MESSAGE_LEN); // msg 전송
            int send_port = cur->client->port;
            printf("Send to %s:%d msg:%s \n", addr, send_port, msg);
        }
        cur = cur->next;
        pthread_mutex_unlock(&mutex);
    }
}

void *handle_client(void *arg)
{
    /*
    사용자로 부터 메시지를 읽는 스레드 구동부입니다.
    각 커넥션 마다 스레드에 해당 함수가 배정됩니다.
    */

    struct connection *connection = (struct connection *)arg;
    struct client_socket *client = connection->client;

    int port = client->port;        // 클라이언트 포트
    char *addr = client->addr;      // 클라이언트 주소
    int my_socket = client->socket; // 소켓

    char msg[256];          // 메시지 전송용 버퍼
    ssize_t bytes_received; // 전달 받은 데이터 크킥

    char firt_msg = 1;

    char name[20];

    while (connection->is_alive)
    {
        // 소켓 데이터 읽기
        bytes_received = read(my_socket, client->buffer, MAX_MESSAGE_LEN);

        // 연결 종료시
        if (bytes_received <= 0)
        {
            connection->is_alive = 0;
            sprintf(msg, "%s 님이 나갔습니다.", name); // 퇴장 메시지
        }
        else if (firt_msg)
        {
            strcpy(name, client->buffer); // 닉네임 저장
            printf("%s %lu\n", name, strlen(name));
            sprintf(msg, "%s 님이 입장하셨습니다.", name);
            firt_msg = 0;
        }
        else if (connection->is_alive)
        {
            printf("Received from: %s:%d  %s \n", addr, port, client->buffer);
            sprintf(msg, "%s >> %s", name, client->buffer); // 전송 메시지 생성
        }

        send_msg(my_socket, addr, msg);                    // 다른 클라이언트들에게 메시지 전송
        memset(client->buffer, 0, strlen(client->buffer)); // 버퍼 클리어
        memset(msg, 0, strlen(msg));                       // msg 버퍼를 비워준다.
    }

    close_socket_connection(); // 연결 종료할 경우 커넥션 자원 수거
    return NULL;
}

int run(int server_socket)
{
    struct sockaddr_in client_address;
    socklen_t client_address_len = sizeof(client_address);
    pthread_t client_thread;

    // 클라이언트 연결 대기
    int client_socket = accept(server_socket, (struct sockaddr *)&client_address, &client_address_len); // 클라이언트 커넥션 할당
    if (client_socket > 0)
    {
        struct connection *new_connection = create_conenction(client_socket, client_address); // 신규 커넥션 객체 생성
        printf("Client connected: %s:%d\n", new_connection->client->addr, new_connection->client->port);

        if (head == NULL) // 첫 연결일 경우
        {
            head = new_connection;
            cur = head;
        }
        else
        {
            pthread_mutex_lock(&mutex);
            cur->next = new_connection; // 연결 리스트 추가
            cur = cur->next;
            pthread_mutex_unlock(&mutex);
        }
        // 멀티 채팅을 위한 스레드 생성
        pthread_create(&client_thread, NULL, handle_client, new_connection);
    }
    return 0;
}

int create_deamon(int server_socket)
{
    pid_t pid, sid;
    int num_clients = 0;

    pid = fork();
    if (pid < 0)
    {
        exit(EXIT_FAILURE);
    }
    if (pid > 0)
    {
        exit(EXIT_SUCCESS);
    }

    // 새 세션 및 프로세스 그룹 생성
    sid = setsid();
    if (sid < 0)
    {
        exit(EXIT_FAILURE);
    }
    while (1)
    {
        run(server_socket);
    }

    // for (int i = 0; i < num_clients; i++)
    // {
    //     pthread_join(client_threads[i], NULL); // 스레드가 종료될 때 까지 대기
    // }
}

int main()
{
    int server_socket, client_socket;
    struct sockaddr_in server_address, client_address;
    socklen_t client_address_len = sizeof(client_address);

    // 서버 소켓 생성
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        printf("setsockopt(SO_REUSEADDR) failed");

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(PORT);

    // 서버 바인딩
    bind(server_socket, (struct sockaddr *)&server_address, sizeof(server_address));

    // 서버 리스닝
    listen(server_socket, 5);
    printf("Server listening on port %d...\n", PORT); // 서버 설정
    create_deamon(server_socket);

    // 부모 프로세스를 종료

    // 서버 소켓 닫기
    close(server_socket);
    return 0;
}
