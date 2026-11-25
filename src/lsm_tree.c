
#include "lsm_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Initialiser le moteur LSM-Tree.
 * Au depart, la MemTable est vide.
 */
int lsm_tree_init(LSMTree *tree)
{
    if (tree == NULL)
    {
        return 0;
    }

    tree->memtable.head = NULL;
    tree->memtable.count = 0;

    return 1;
}

/*
 * SET : inserer ou mettre a jour une paire cle-valeur.
 * Les cles sont conservees dans l'ordre alphabetique.
 */
int lsm_tree_set(LSMTree *tree, const char *key, int value)
{
    if (tree == NULL || key == NULL)
    {
        return 0;
    }

    size_t key_length = strlen(key);

    /* Verifier que la cle respecte la taille autorisee */
    if (key_length == 0 || key_length >= LSM_KEY_SIZE)
    {
        return 0;
    }

    MemTableNode **current = &tree->memtable.head;

    /* Chercher la position de la cle dans la liste triee */
    while (*current != NULL &&
           strcmp((*current)->key, key) < 0)
    {
        current = &(*current)->next;
    }

    /* Si la cle existe deja, mettre a jour sa valeur */
    if (*current != NULL &&
        strcmp((*current)->key, key) == 0)
    {
        (*current)->value = value;
        return 1;
    }

    /* Sinon, creer un nouveau noeud */
    MemTableNode *new_node = malloc(sizeof(MemTableNode));

    if (new_node == NULL)
    {
        return 0;
    }

    /* Remplir le nouveau noeud */
    strcpy(new_node->key, key);
    new_node->value = value;

    /* Inserer le noeud a sa position triee */
    new_node->next = *current;
    *current = new_node;

    /* Incrementer le nombre de cles */
    tree->memtable.count++;

    return 1;
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
