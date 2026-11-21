
#include <stdio.h>
#include <string.h>
#include "storage.h"

int main(void)
{
    char input[100];
    char operation[16];
    char key[50];
    int value;

    printf("MiniDB started!\n");

    while (1)
    {
        printf("MiniDB> ");

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

        if (sscanf(input, "%15s", operation) != 1)
        {
            continue;
        }

        to_uppercase((char *)operation);

        if (strcmp(operation, "SET") == 0)
        {
            if (sscanf(input, "%15s %49s %d",
                       operation, key, &value) == 3)
            {
                if (storage_set(key, value))
                {
                    printf("OK: %s = %d\n", key, value);
                }
                else
                {
                    printf("Error: failed to store data\n");
                }
            }
            else
            {
                printf("Usage: SET <key> <value>\n");
            }
        }

        else if (strcmp(operation, "GET") == 0)
        {
            if (sscanf(input, "%15s %49s", operation, key) == 2)
            {
                int result;

                if (storage_get(key, &result))
                {
                    printf("%s = %d\n", key, result);
                }
                else
                {
                    printf("Key not found: %s\n", key);
                }
            }
            else
            {
                printf("Usage: GET <key>\n");
            }
        }

        else if (strcmp(operation, "EXIT") == 0)
        {
            break;
        }
        else
        {
            printf("Unknown command\n");
        }
    }

    printf("MiniDB closed.\n");

    return 0;
}
