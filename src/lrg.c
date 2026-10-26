#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

unsigned char ascii_lower(unsigned char ch)
{
    if (ch >= 'A' && ch <= 'Z')
        return ch + ('a' - 'A');
    return ch;
}

/* 返回 1 表示匹配，0 表示不匹配 */
int line_matches(const char *line, const char *pattern, int ignore_case)
{
    if (!ignore_case)
        return strstr(line, pattern) != NULL;

    if (pattern[0] == '\0')
        return 1;

    /* start：尝试匹配的起点；j：当前比较的模式字符位置 */
    for (size_t start = 0; line[start] != '\0'; start++) {
        size_t j = 0;

        while (pattern[j] != '\0' &&
               line[start + j] != '\0' &&
               ascii_lower((unsigned char)line[start + j]) ==
               ascii_lower((unsigned char)pattern[j])) {
            j++;
        }

        if (pattern[j] == '\0')
            return 1;
    }

    return 0;
}

/*
 * show_filename：是否输出文件名前缀
 * line_number：当前行号
 * 返回：-1 输出失败；0 不匹配；1 匹配
 */
int handle_line(const char *line, const char *pattern,
                int ignore_case, const char *filename,
                int show_filename, int show_line_number,
                int count_only, size_t line_number)
{
    if (!line_matches(line, pattern, ignore_case))
        return 0;

    if (!count_only) {
        if (show_filename && printf("%s:", filename) < 0)
            return -1;

        if (show_line_number && printf("%zu:", line_number) < 0)
            return -1;

        if (printf("%s\n", line) < 0)
            return -1;
    }

    return 1;
}

/* 返回：-1 出错；0 没有匹配；1 至少一行匹配。fd 由 main 关闭。 */
int process_fd(int fd, const char *filename, const char *pattern,
               int ignore_case, int show_line_number,
               int count_only, int show_filename)
{
    char buffer[4096];       // read 的临时缓冲区
    size_t len = 0;          // 当前行的字符数
    size_t capacity = 128;   // 行缓冲区容量，包括字符串终止符
    size_t line_number = 1;  // 当前输入的行号
    size_t match_count = 0;  // 当前输入的匹配行数
    int result;             // handle_line 的处理结果

    char *line = malloc(capacity);
    if (line == NULL) {
        fprintf(stderr, "lrg: %s: %s\n", filename, strerror(errno));
        return -1;
    }
    line[0] = '\0';

    while (1) {
        ssize_t nread = read(fd, buffer, sizeof(buffer));

        if (nread > 0) {
            for (ssize_t j = 0; j < nread; j++) {
                char ch = buffer[j];

                if (ch == '\n') {
                    result = handle_line(
                        line, pattern, ignore_case, filename,
                        show_filename, show_line_number,
                        count_only, line_number
                    );

                    if (result == -1) {
                        fprintf(stderr, "lrg: standard output: %s\n",
                                strerror(errno));
                        free(line);
                        return -1;
                    }

                    match_count += result;
                    line_number++;
                    len = 0;
                    line[0] = '\0';
                } else {
                    if (len == capacity - 1) {
                        if (capacity > SIZE_MAX / 2) {
                            fprintf(stderr, "lrg: %s: line too long\n",
                                    filename);
                            free(line);
                            return -1;
                        }

                        size_t new_capacity = capacity * 2;
                        char *temp = realloc(line, new_capacity);

                        if (temp == NULL) {
                            fprintf(stderr, "lrg: %s: %s\n",
                                    filename, strerror(errno));
                            free(line);
                            return -1;
                        }

                        line = temp;
                        capacity = new_capacity;
                    }

                    line[len++] = ch;
                    line[len] = '\0';
                }
            }
        } else if (nread == 0) {
            break;
        } else {
            if (errno == EINTR)
                continue;

            fprintf(stderr, "lrg: %s: %s\n", filename, strerror(errno));
            free(line);
            return -1;
        }
    }

    /* 处理末尾没有换行符的最后一行 */
    if (len > 0) {
        result = handle_line(
            line, pattern, ignore_case, filename,
            show_filename, show_line_number,
            count_only, line_number
        );

        if (result == -1) {
            fprintf(stderr, "lrg: standard output: %s\n", strerror(errno));
            free(line);
            return -1;
        }

        match_count += result;
    }

    free(line);

    if (count_only) {
        if ((show_filename && printf("%s:", filename) < 0) ||
            printf("%zu\n", match_count) < 0) {
            fprintf(stderr, "lrg: standard output: %s\n", strerror(errno));
            return -1;
        }
    }

    return match_count > 0;
}

int main(int argc, char *argv[])
{
    int show_line_number = 0; // -n：显示行号
    int ignore_case = 0;      // -i：忽略 ASCII 大小写
    int count_only = 0;       // -c：只输出匹配行数
    int i = 1;               // 当前解析的参数位置

    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        for (int j = 1; argv[i][j] != '\0'; j++) {
            if (argv[i][j] == 'n')
                show_line_number = 1;
            else if (argv[i][j] == 'i')
                ignore_case = 1;
            else if (argv[i][j] == 'c')
                count_only = 1;
            else {
                fprintf(stderr, "usage: lrg [-nic] PATTERN [FILE...]\n");
                return 2;
            }
        }
        i++;
    }

    if (i == argc) {
        fprintf(stderr, "usage: lrg [-nic] PATTERN [FILE...]\n");
        return 2;
    }

    const char *pattern = argv[i++]; // 匹配模式
    int first_file = i;              // 第一个文件参数的位置
    int file_count = argc - i;       // 文件参数个数
    int had_error = 0;               // 是否出现过错误
    int any_match = 0;               // 是否至少有一个输入匹配

    if (file_count == 0) {
        /* result：本次 process_fd 的返回值 */
        int result = process_fd(
            STDIN_FILENO, "standard input", pattern,
            ignore_case, show_line_number, count_only, 0
        );

        if (result == -1)
            had_error = 1;
        else if (result == 1)
            any_match = 1;
    } else {
        for (int k = first_file; k < argc; k++) {
            const char *filename = argv[k];
            int fd = open(filename, O_RDONLY);

            if (fd == -1) {
                fprintf(stderr, "lrg: %s: %s\n", filename, strerror(errno));
                had_error = 1;
                continue;
            }

            int result = process_fd(
                fd, filename, pattern, ignore_case,
                show_line_number, count_only, file_count > 1
            );

            if (result == -1)
                had_error = 1;
            else if (result == 1)
                any_match = 1;

            if (close(fd) == -1) {
                fprintf(stderr, "lrg: %s: %s\n", filename, strerror(errno));
                had_error = 1;
            }
        }
    }

    /* printf 有缓冲，最终刷新时也可能失败 */
    if (fflush(stdout) == EOF) {
        fprintf(stderr, "lrg: standard output: %s\n", strerror(errno));
        had_error = 1;
    }

    if (had_error)
        return 2;
    return any_match ? 0 : 1;
}