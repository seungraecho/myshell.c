#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_CMD_LEN 1024
#define MAX_ARGS 128
#define MAX_PATHS 64
#define CONFIG_FILE ".myshell"

int parse_config_paths(char *pathPtr) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    // 파일이 없을 경우에 대한 예외 처리
    if (fp == NULL) {
        perror("Configuration file (.myshell) open failed");
        return 0; // 로드된 경로 없음
    }

    char line;
    int path_count = 0;

    // 파일의 끝까지 읽음
    while (fgets(line, sizeof(line), fp)) {
        // 개행 문자 제거
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n')
            line[len-1] = '\0';

        // "PATH=" 문자열로 시작하는지 확인
        if (strncmp(line, "PATH=", 5) == 0) {
            // "PATH=" 이후의 문자열 포인터 획득

            
            while (token!= NULL && path_count < MAX_PATHS) {
                // 토큰을 복제하여 저장 (strdup은 malloc을 사용하므로 추후 free 필요)
                // 본 예제에서는 쉘 종료 시 OS에 의한 회수를 가정하거나 별도 해제 로직 필요
                pathPtr[path_count] = strdup(token);
                path_count++;
                token = strtok(NULL, ":");
            }
            break; // 첫 번째 PATH 설정만 유효하다고 가정
        }
    }
    fclose(fp);
    return path_count;
}

int main() {
    char line;
    char *pathPtr;
    char *argv;
    int path_count = 0;

    // 1. 설정 로드 단계
    path_count = parse_config_paths(pathPtr);
    
    // 설정 파일이 없거나 PATH가 없는 경우 기본값 설정 (안전장치)
    if (path_count == 0) {
        fprintf(stderr, "Warning: Using default paths (/bin, /usr/bin, /usr/local/bin)\n");
        pathPtr = "/bin";
        pathPtr = "/usr/bin";
        pathPtr = "/usr/local/bin";
        path_count = 3;
    }

    while (1) {
        // 프롬프트 출력
        printf("myshell%% ");
        
        // 입력 대기 (EOF 처리 포함)
        if (fgets(line, MAX_CMD_LEN, stdin) == NULL) {
            printf("\nLog out\n");
            break;
        }

        // 빈 입력 처리 (엔터만 쳤을 때)
        if (line == '\n') continue;

        // 개행 문자 제거
        line[strcspn(line, "\n")] = 0;

        // 3. 명령어 파싱 (공백 기준 분리)
        int argc = 0;
        char *token = strtok(line, " ");
        while (token!= NULL && argc < MAX_ARGS - 1) {
            argv[argc++] = token;
            token = strtok(NULL, " ");
        }
        argv[argc] = NULL; // execv를 위해 인자 배열의 끝은 NULL이어야 함

        if (argv == NULL) continue; // 공백만 입력된 경우

        // 4. 내장 명령어 처리
        // 종료 명령
        if (strcmp(argv, "exit") == 0 || strcmp(argv, "quit") == 0) {
            printf("Terminating myshell...\n");
            break;
        }
        
        // 디렉토리 변경 (cd)
        if (strcmp(argv, "cd") == 0) {
            // 인자가 없으면 홈 디렉토리로, 있으면 해당 디렉토리로 이동
            char *target_dir = argv;
            if (target_dir == NULL) {
                target_dir = getenv("HOME"); // HOME 환경변수 사용
            }
            
            if (chdir(target_dir)!= 0) {
                perror("cd failed");
            }
            continue; // 자식 프로세스 생성 없이 루프 재개
        }

        // 5. 외부 명령어 탐색 및 실행
        char exec_path;
        int found = 0;

        // 5-1. 절대 경로 또는 상대 경로로 직접 입력된 경우 (예: /bin/ls,./a.out)
        if (strchr(argv, '/')!= NULL) {
            if (access(argv, F_OK) == 0) {
                if (access(argv, X_OK) == 0) { // 실행 권한 확인
                    strncpy(exec_path, argv, MAX_CMD_LEN);
                    found = 1;
                } else {
                    fprintf(stderr, "Permission denied: %s\n", argv);
                    continue;
                }
            }
        } 
        // 5-2. PATH 경로 탐색
        else {
            for (int i = 0; i < path_count; i++) {
                // 경로 생성: path + "/" + command
                snprintf(exec_path, sizeof(exec_path), "%s/%s", pathPtr[i], argv);
                
                if (access(exec_path, F_OK) == 0) {
                     if (access(exec_path, X_OK) == 0) {
                        found = 1;
                        break; // 찾았으면 탐색 중단
                     }
                }
            }
        }

        if (found) {
            // 6. 프로세스 생성 및 실행
            pid_t pid = fork();

            if (pid < 0)
                perror("fork error");
	    else if (pid == 0) {
                // [자식 프로세스]
                // execv 호출: 찾은 경로와 인자 배열 전달
                if (execv(exec_path, argv) == -1) {
                    // execv는 성공 시 리턴하지 않으므로, 여기까지 오면 에러임
                    perror("Execution failed");
                    exit(EXIT_FAILURE); // 자식 프로세스 종료
                }
            }
	    else {
                // [부모 프로세스]
                int status;
                // 자식 프로세스 종료 대기
                if (wait(&status) == -1) {
                    perror("wait error");
                }
                // 선택적: 종료 상태 코드 확인 로직 추가 가능
            }
        } 
	else
            printf("Command not found: %s\n", argv);
    }

    // 메모리 정리 (strdup으로 할당된 경로들)
    for(int i=0; i<path_count; i++) {
        if(pathPtr[i]!= NULL)
	       free(pathPtr[i]);
    }

    return 0;
}
