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

int parse_config_paths(char *pathPtr[]) {
    char *token;
    FILE *fp = fopen(CONFIG_FILE, "r");
    // 파일이 없을 경우에 대한 예외 처리
    if (fp == NULL) {
        perror("Configuration file (.myshell) open failed");
        return 0; // 로드된 경로 없음
    }

    char line[MAX_CMD_LEN];
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
            token = strtok(line + 5, ":");

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

int main(int argc, char *argv[]) {
    char line[MAX_CMD_LEN];
    char *pathPtr[MAX_ARGS];
    int path_count = 0;

    // 1. 설정 로드 단계
    path_count = parse_config_paths(pathPtr);

    // 설정 파일이 없거나 PATH가 없는 경우 기본값 설정 (안전장치)
    if (path_count == 0) {
        fprintf(stderr, "Warning: Using default paths (/bin, /usr/bin, /usr/local/bin)\n");
        pathPtr[0] = "/bin";
        pathPtr[1] = "/usr/bin";
        pathPtr[2] = "/usr/local/bin";
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
        if (line[0] == '\n') continue;

        // 개행 문자 제거
        line[strcspn(line, "\n")] = 0;
		
		// 루프마다 새로운 인자 배열 사용
		int my_argc = 0;
		char *my_argv[MAX_ARGS];

        // 3. 명령어 파싱 (공백 기준 분리)
        char *token = strtok(line, " ");
        while (token!= NULL && my_argc < MAX_ARGS - 1) {
            my_argv[my_argc++] = token;
            token = strtok(NULL, " ");
        }
        my_argv[argc] = NULL; // execv를 위해 인자 배열의 끝은 NULL이어야 함

        if (my_argv[0] == NULL) continue; // 공백만 입력된 경우

        // 4. 내장 명령어 처리
        // 종료 명령
        if (strcmp(my_argv[0], "exit") == 0 || strcmp(my_argv[0], "quit") == 0) {
            break;
        }

        // 디렉토리 변경 (cd)
        if (strcmp(my_argv[0], "cd") == 0) {
            // 인자가 없으면 홈 디렉토리로, 있으면 해당 디렉토리로 이동
            char *target_dir = my_argv[1];
            if (target_dir == NULL) {
                target_dir = getenv("HOME"); // HOME 환경변수 사용
            }

            if (chdir(target_dir)!= 0) {
                perror("cd failed");
            }
            continue; // 자식 프로세스 생성 없이 루프 재개
        }

        // 5. 외부 명령어 탐색 및 실행
        char exec_path[MAX_CMD_LEN];
        int found = 0;

        // 5-1. 절대 경로 또는 상대 경로로 직접 입력된 경우 (예: /bin/ls,./a.out)
        if (strchr(my_argv[0], '/')!= NULL) {
			if (access(my_argv[0], X_OK) == 0) { // 실행 권한 확인
				strncpy(exec_path, my_argv[0], MAX_CMD_LEN);
			    found = 1;
            }
		}
        // 5-2. PATH 경로 탐색
        else {
            for (int i = 0; i < path_count; i++) {
                // 경로 생성: path + "/" + command
                snprintf(exec_path, sizeof(exec_path), "%s/%s", pathPtr[i], my_argv[0]);
				if (access(exec_path, X_OK) == 0) {
                    found = 1;
                    break; // 찾았으면 탐색 중단
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
                if (execv(exec_path, argv, argv+1) == -1) {
                    perror("Execution failed");
                    exit(EXIT_FAILURE);
				}
            }
            else {
                // [부모 프로세스]
                int status;
                if (wait(&status) == -1) {
                    perror("wait error");
                }
            }
        }
        else
            printf("Command not found: %s\n", argv[0]);
    }

    // 메모리 정리 (strdup으로 할당된 경로들)
    for(int i=0; i<path_count; i++) {
        if(pathPtr[i]!= NULL)
               free(pathPtr[i]);
    }
    return 0;
}
