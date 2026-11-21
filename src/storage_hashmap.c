#include <stdio.h>
#include <string.h>

#include "hashmap.h"


#define DATA_FILE "data/data.log"

static HashMap index_map;

int storage_hashmap_init(void){
    hashmap_init(&index_map);

    FILE *file = fopen(DATA_FILE,"r");

    if(file == NULL){
        file = fopen(DATA_FILE, "a");

        if(file == NULL){
            perror("Erreur ouverture data.log");
            return 0;
        }

        fclose(file);

        file = fopen(DATA_FILE,"r");

        if(file == NULL){
            perror("Erreur ouverture data.log");
            return 0;
        }
    }

    char line[256];
    long offset;

    while(1){
        offset = ftell(file);

        if(fgets(line,sizeof(line),file)==NULL){
            break;
        }

        char key[HASHMAP_KEY_SIZE];
        int value;

        if (sscanf(line,"%49s %d",key,&value) == 2){
            hashmap_put(&index_map,key,offset);
        }
    }

    fclose(file);
    printf("Storage initialise : HashMap reconstruite.\n");
    return 1;
}






int storage_hashmap_set(const char *key, int value)
{
    if (key == NULL || strlen(key) >= HASHMAP_KEY_SIZE)
    {
        return 0;
    }

    FILE *file = fopen(DATA_FILE, "a");

    if (file == NULL)
    {
        perror("Erreur ouverture data.log");
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    long offset = ftell(file);

    if (offset == -1L)
    {
        fclose(file);
        return 0;
    }
    if (fprintf(file, "%s %d\n", key, value) < 0)
    {
        fclose(file);
        return 0;
    }
    if (fclose(file) != 0)
    {
        return 0;
    }
    if (!hashmap_put(&index_map, key, offset))
    {
        return 0;
    }

    return 1;
}








int storage_hashmap_get(const char *key, int *value)
{
    if (key == NULL || value == NULL)
    {
        return 0;
    }

    long offset;
    if (!hashmap_get(&index_map, key, &offset))
    {
        return 0;
    }
    FILE *file = fopen(DATA_FILE, "r");

    if (file == NULL)
    {
        perror("Erreur ouverture data.log");
        return 0;
    }
    if (fseek(file, offset, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }
    char stored_key[HASHMAP_KEY_SIZE];
    int stored_value;

    if (fscanf(file, "%49s %d", stored_key, &stored_value) != 2)
    {
        fclose(file);
        return 0;
    }

    fclose(file);
    if (strcmp(stored_key, key) != 0)
    {
        return 0;
    }

    *value = stored_value;

    return 1;
}


void storage_hashmap_close(void)
{
    hashmap_destroy(&index_map);
}