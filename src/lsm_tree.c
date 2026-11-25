
#include "lsm_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
 * GET : rechercher une cle dans la MemTable.
 * Retourner 1 si elle existe, sinon 0.
 */
int lsm_tree_get(const LSMTree *tree, const char *key, int *value)
{
    if (tree == NULL || key == NULL || value == NULL)
    {
        return 0;
    }

    const MemTableNode *current = tree->memtable.head;

    while (current != NULL)
    {
        int comparison = strcmp(current->key, key);

        /* Cle trouvee */
        if (comparison == 0)
        {
            *value = current->value;
            return 1;
        }

        /* Arreter si la cle recherchee a ete depassee */
        if (comparison > 0)
        {
            return 0;
        }

        current = current->next;
    }

    return 0;
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
