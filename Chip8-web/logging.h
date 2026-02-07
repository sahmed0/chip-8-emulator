#ifndef LOGGING_H
#define LOGGING_H

#include <stdio.h>

#define LOG_INFO(...)                                                          \
  do {                                                                         \
    printf("[INFO]  ");                                                        \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
  } while (0)
#define LOG_WARN(...)                                                          \
  do {                                                                         \
    printf("[WARN]  ");                                                        \
    printf(__VA_ARGS__);                                                       \
    printf("\n");                                                              \
  } while (0)
#define LOG_ERROR(...)                                                         \
  do {                                                                         \
    fprintf(stderr, "[ERROR] ");                                               \
    fprintf(stderr, __VA_ARGS__);                                              \
    fprintf(stderr, "\n");                                                     \
  } while (0)

#endif // LOGGING_H
