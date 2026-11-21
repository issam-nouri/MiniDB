#include <stdio.h>
#include <string.h>
#include "storage.h"

int storage_set(const char *key, int value)
{
    FILE *file = fopen("data/data.log", "a");

    if (file == NULL)
    {
        perror("Error opening data.log");
        return 0;
    }

    fprintf(file, "%s %d\n", key, value);

    fclose(file);

    return 1;
}


int storage_get(const char *key, int *value)
{
    FILE *file = fopen("data/data.log", "r");

    if (file == NULL)
    {
        perror("Error opening data.log");
        return 0;
    }

    char line[256];
    char stored_key[100];
    int stored_value;

    int found = 0;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        if (sscanf(line, "%99s %d", stored_key, &stored_value) == 2)
        {
            if (strcmp(stored_key, key) == 0)
            {
                *value = stored_value;
                found = 1;
            }
        }
    }

    fclose(file);

    return found;
}

