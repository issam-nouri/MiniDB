#include "lsm_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
static int lsm_wal_recover(LSMTree *tree);

static int lsm_apply_set(LSMTree *tree,
                         const char *key,
                         int value,
                         int write_wal);
/*
 * Ajouter une operation SET au journal WAL.
 * Retourne 1 en cas de succes, 0 en cas d'erreur.
 */
static int lsm_wal_append_set(const char *key, int value)
{
    FILE *file = fopen(LSM_WAL_PATH, "a");

    if (file == NULL)
    {
        perror("Erreur ouverture WAL");
        return 0;
    }

    int success = 1;

    if (fprintf(file, "SET %s %d\n", key, value) < 0)
    {
        success = 0;
    }

    if (fclose(file) == EOF)
    {
        success = 0;
    }

    return success;
}


/*
 * Initialiser le moteur LSM-Tree.
 * Reconstruire la MemTable a partir du WAL.
 */
int lsm_tree_init(LSMTree *tree)
{
    if (tree == NULL)
    {
        return 0;
    }

    /* Initialiser une MemTable vide */
    tree->memtable.head = NULL;
    tree->memtable.count = 0;

    /* Recuperer les donnees du WAL */
    if (!lsm_wal_recover(tree))
    {
        /* Liberer les noeuds crees si le recovery echoue */
        lsm_tree_close(tree);
        return 0;
    }

    return 1;
}


/*
 * WAL Recovery : reconstruire la MemTable
 * a partir des operations enregistrees.
 */
static int lsm_wal_recover(LSMTree *tree)
{
    FILE *file = fopen(LSM_WAL_PATH, "r");

    if (file == NULL)
    {
        /* Premier demarrage : aucun WAL n'existe */
        if (errno == ENOENT)
        {
            return 1;
        }

        perror("Erreur lecture WAL");
        return 0;
    }

    char line[256];
    char key[LSM_KEY_SIZE];
    char extra;
    int value;
    int success = 1;

    while (fgets(line, sizeof(line), file) != NULL)
    {
        /* Verifier que la ligne est complete */
        if (strchr(line, '\n') == NULL)
        {
            fprintf(stderr, "WAL : ligne incomplete\n");
            success = 0;
            break;
        }

        /* Lire une operation SET */
        if (strncmp(line, "SET ", 4) != 0 ||
            sscanf(line + 4, "%49s %d %c",
                   key, &value, &extra) != 2)
        {
            fprintf(stderr, "WAL : operation invalide\n");
            success = 0;
            break;
        }

        /* Reconstruire en RAM sans reecrire le WAL */
        if (!lsm_apply_set(tree, key, value, 0))
        {
            success = 0;
            break;
        }
    }

    if (ferror(file))
    {
        success = 0;
    }

    if (fclose(file) == EOF)
    {
        success = 0;
    }

    return success;
}

/*
 * Appliquer un SET dans la MemTable.
 *
 * write_wal = 1 : enregistrer dans le WAL
 * write_wal = 0 : ne pas enregistrer (recovery)
 */
static int lsm_apply_set(LSMTree *tree, const char *key,
                         int value, int write_wal)
{
    if (tree == NULL || key == NULL)
    {
        return 0;
    }

    size_t key_length = strlen(key);

    if (key_length == 0 || key_length >= LSM_KEY_SIZE)
    {
        return 0;
    }

    MemTableNode **current = &tree->memtable.head;

    /* Chercher la position dans la liste triee */
    while (*current != NULL &&
           strcmp((*current)->key, key) < 0)
    {
        current = &(*current)->next;
    }

    /* Cas 1 : la cle existe deja */
    if (*current != NULL &&
        strcmp((*current)->key, key) == 0)
    {
        if (write_wal && !lsm_wal_append_set(key, value))
        {
            return 0;
        }

        (*current)->value = value;
        return 1;
    }

    /* Cas 2 : nouvelle cle */
    MemTableNode *new_node = malloc(sizeof(MemTableNode));

    if (new_node == NULL)
    {
        return 0;
    }

    strcpy(new_node->key, key);
    new_node->value = value;

    if (write_wal && !lsm_wal_append_set(key, value))
    {
        free(new_node);
        return 0;
    }

    new_node->next = *current;
    *current = new_node;

    tree->memtable.count++;

    return 1;
}

/*
 * SET normal : enregistrer dans le WAL,
 * puis appliquer dans la MemTable.
 */
int lsm_tree_set(LSMTree *tree, const char *key, int value)
{
    return lsm_apply_set(tree, key, value, 1);
}


/*
 * Rechercher une cle dans les SSTables.
 * Conserver la valeur de la SSTable ayant le plus grand ID.
 *
 * Hypothese temporaire : les IDs ne sont pas reutilises.
 * Retourne 1 si la cle existe, sinon 0.
 */
static int lsm_sstable_get(const char *key, int *value)
{
    DIR *dir = opendir(LSM_SSTABLE_DIR);

    if (dir == NULL)
    {
        if (errno != ENOENT)
            perror("Erreur ouverture dossier SSTables");
        return 0;
    }

    unsigned int newest_id = 0;
    int newest_value = 0;
    int found = 0;
    int error = 0;
    struct dirent *entry;

    for (;;)
    {
        errno = 0;
        entry = readdir(dir);

        if (entry == NULL)
        {
            if (errno != 0)
            {
                perror("Erreur lecture dossier SSTables");
                error = 1;
            }
            break;
        }

        const char *name = entry->d_name;

        /* Accepter uniquement sst_XXXXXX.sst */
        if (strlen(name) != 14 ||
            strncmp(name, "sst_", 4) != 0 ||
            strcmp(name + 10, ".sst") != 0)
        {
            continue;
        }

        unsigned int id = 0;

        for (int i = 4; i < 10; i++)
        {
            if (name[i] < '0' || name[i] > '9')
            {
                id = 0;
                break;
            }

            id = id * 10u +
                 (unsigned int)(name[i] - '0');
        }

        /* Ignorer les SSTables plus anciennes */
        if (id == 0 || id <= newest_id)
            continue;

        char path[160];

        int n = snprintf(path, sizeof(path),
                         "%s/%s", LSM_SSTABLE_DIR, name);

        if (n < 0 || n >= (int)sizeof(path))
        {
            error = 1;
            break;
        }

        FILE *file = fopen(path, "r");

        if (file == NULL)
        {
            perror("Erreur ouverture SSTable");
            error = 1;
            break;
        }

        char line[256];
        char file_key[LSM_KEY_SIZE];
        char extra;
        int file_value;
        int file_error = 0;

        /* Parcourir les cles triees du fichier */
        while (fgets(line, sizeof(line), file) != NULL)
        {
            if (strchr(line, '\n') == NULL ||
                sscanf(line, "%49s %d %c",
                       file_key, &file_value, &extra) != 2)
            {
                file_error = 1;
                break;
            }

            int comparison = strcmp(file_key, key);

            if (comparison == 0)
            {
                newest_id = id;
                newest_value = file_value;
                found = 1;
                break;
            }

            /* Le fichier est trie */
            if (comparison > 0)
                break;
        }

        if (ferror(file))
            file_error = 1;

        if (fclose(file) == EOF)
            file_error = 1;

        if (file_error)
        {
            fprintf(stderr,
                    "Erreur de lecture SSTable : %s\n", path);
            error = 1;
            break;
        }
    }

    if (closedir(dir) != 0)
        error = 1;

    if (error)
        return 0;

    if (found)
        *value = newest_value;

    return found;
}
 
/*
 * GET : rechercher une cle dans la MemTable.
 * Retourner 1 si elle existe, sinon 0.
 */
int lsm_tree_get(const LSMTree *tree,
                 const char *key,
                 int *value)
{
    if (tree == NULL || key == NULL || value == NULL)
    {
        return 0;
    }

    /* Etape 1 : chercher dans la MemTable */
    const MemTableNode *current = tree->memtable.head;

    while (current != NULL)
    {
        int comparison = strcmp(current->key, key);

        if (comparison == 0)
        {
            *value = current->value;
            return 1;
        }

        if (comparison > 0)
        {
            break;
        }

        current = current->next;
    }

    /* Etape 2 : chercher dans les SSTables */
    return lsm_sstable_get(key, value);
}

/*
 * Fermer le moteur et liberer la memoire.
 */
void lsm_tree_close(LSMTree *tree)
{
    if (tree == NULL)
    {
        return;
    }

    MemTableNode *current = tree->memtable.head;

    while (current != NULL)
    {
        MemTableNode *next = current->next;

        free(current);

        current = next;
    }

    tree->memtable.head = NULL;
    tree->memtable.count = 0;
}


/*
 * Ecrire la MemTable triee dans une nouvelle SSTable.
 *
 * Cette premiere version conserve la MemTable et le WAL.
 * Retourne 1 si la SSTable est creee, sinon 0.
 */
int lsm_tree_write_sstable(const LSMTree *tree)
{
    if (tree == NULL || tree->memtable.head == NULL)
    {
        return 0;
    }

    /* Trouver un nom de fichier disponible */
    for (unsigned int id = 1; id <= 999999; ++id)
    {
        char final_path[160];
        char temp_path[160];

        int a = snprintf(final_path, sizeof(final_path),
                         "%s/sst_%06u.sst",
                         LSM_SSTABLE_DIR, id);

        int b = snprintf(temp_path, sizeof(temp_path),
                         "%s/sst_%06u.tmp",
                         LSM_SSTABLE_DIR, id);

        if (a < 0 || b < 0 ||
            a >= (int)sizeof(final_path) ||
            b >= (int)sizeof(temp_path))
        {
            return 0;
        }

        struct stat info;

        if (stat(final_path, &info) == 0)
        {
            continue;
        }

        if (errno != ENOENT)
        {
            perror("Erreur verification SSTable");
            return 0;
        }

        /* Creer un fichier temporaire sans ecraser
           un fichier existant */
        FILE *file = fopen(temp_path, "wx");

        if (file == NULL)
        {
            if (errno == EEXIST)
            {
                continue;
            }

            perror("Erreur creation SSTable");
            return 0;
        }

        int success = 1;

        /* Parcourir la liste chainee triee */
        for (const MemTableNode *node = tree->memtable.head;
             node != NULL;
             node = node->next)
        {
            if (fprintf(file, "%s %d\n",
                        node->key, node->value) < 0)
            {
                success = 0;
                break;
            }
        }

        if (fclose(file) == EOF)
        {
            success = 0;
        }

        if (!success)
        {
            remove(temp_path);
            return 0;
        }

        /* Publier la SSTable seulement apres
           la fin de son ecriture */
        if (rename(temp_path, final_path) != 0)
        {
            perror("Erreur publication SSTable");
            remove(temp_path);
            return 0;
        }

        return 1;
    }

    return 0;
}

