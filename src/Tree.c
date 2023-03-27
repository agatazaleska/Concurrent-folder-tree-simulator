#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include "Tree.h"
#include "HashMap.h"
#include "path_utils.h"
#include "err.h"

#define MY_ERROR -1

/*
 * Zastosowany mechanizm synchronizacji:
 *
 * Korzystam z rozwiązania problemów czytelników i pisarzy z laboratoriów.
 * Każdy wierzchołek drzewa jest także czytelnią w której mogą przebywać czytelnicy i pisarze.
 * Jeśli podczas operacji na drzewie hashmapa w wierzchołku jest modyfikowana, przebywa tam pisarz.
 * Jeśli natomiast potrzebujemy tylko odczytać coś z hashmapy w wierzchołku, przebywa tam czytelnik.
 *
 * Podczas operacji na drzewie "blokuję" ścieżkę do folderu docelowego w danej operacji w następujący sposób:
 * umieszczam czytelników na całej ścieżce do docelowego folderu, w celu
 * zagwarantowania poprawnego wykonywania operacji hmap_get (te operacje mogą wykonywać się ze sobą współbieżnie)
 * oraz zapewnienia bezpieczeństwa aby nic nie zmieniło się na interesującej nas ścieżce podczas wykonywania operacji.
 * W ten sposób na całej ścieżce nie będzie napewno żadnego pisarza.
 *
 * w docelowym wierzchołku umieszczam czytelnika (dla funkcji tree_list) oraz
 * pisarza dla pozostałych funkcji (operacje hmap_remove, hmap_insert nie mogą wykonywać się współbieżnie
 * z innymi operacjami na mapie).
 *
 * Po zakończeniu funkcji lub otrzymania błędu, wypuszczam czytelników i pisarzy z wierzchołków.
 * Protokoły pisarzy i czytelników są wzorowane na rozwiązaniu z laboratoriów.
 */

struct Tree {
    Tree *ancestor;
    HashMap *contents;

    // "czytelnia", czyli
    // zmienne potrzebne do synchronizacji dostępu do wierzchołka
    pthread_mutex_t mutex;
    pthread_cond_t readers;
    pthread_cond_t writers;
    int readers_number, writers_number, waiting_readers, waiting_writers;
    int whose_turn;
};
typedef struct Tree Tree;

// protokół początkowy czytelnika
static void start_reading(Tree *T) {
    int err;
    if ((err = pthread_mutex_lock(&T->mutex)) != 0)
        syserr ("mutex failed", err);

    while (T->writers_number == 1 || (T->waiting_writers > 0 && T->whose_turn == 1)) {
        T->waiting_readers += 1;
        if ((err = pthread_cond_wait(&T->readers, &T->mutex)) != 0)
            syserr ("cond wait failed", err);

        T->waiting_readers -= 1;
    }
    T->readers_number += 1;

    if (T->waiting_readers > 0) {
        if ((err = pthread_cond_signal(&T->readers)) != 0)
            syserr ("cond signal failed", err);
    }
    if ((err = pthread_mutex_unlock(&T->mutex)) != 0)
        syserr ("mutex failed", err);
}

// protokół końcowy czytelnika
static void finish_reading(Tree *T) {
    int err;
    if ((err = pthread_mutex_lock(&T->mutex)) != 0)
        syserr("mutex failed", err);

    T->readers_number -= 1;

    if (T->readers_number == 0 && T->waiting_writers > 0) {
        if ((err = pthread_cond_signal(&T->writers)) != 0)
            syserr("cond signal failed", err);
    }
    if ((err = pthread_mutex_unlock(&T->mutex)) != 0)
        syserr("mutex failed", err);
}

// protokół początkowy pisarza
static void start_writing(Tree *T) {
    int err;
    if ((err = pthread_mutex_lock(&T->mutex)) != 0)
        syserr ("mutex failed", err);

    T->whose_turn = 1;

    while (T->readers_number > 0 || T->writers_number == 1) {
        T->waiting_writers += 1;

        if ((err = pthread_cond_wait(&T->writers, &T->mutex)) != 0)
            syserr ("cond wait failed", err);

        T->waiting_writers -= 1;
    }
    T->writers_number += 1;

    if ((err = pthread_mutex_unlock(&T->mutex)) != 0) {
        syserr ("mutex failed", err);
    }
}

// protokół końcowy pisarza
static void finish_writing(Tree *T) {
    int err;
    if ((err = pthread_mutex_lock(&T->mutex)) != 0)
        syserr ("mutex failed", err);

    T->writers_number -= 1;

    if (T->waiting_readers > 0)  {
        T->whose_turn = 0;

        if ((err = pthread_cond_signal(&T->readers)) != 0)
            syserr ("cond signal failed", err);
    }
    else if (T->waiting_writers > 0) {
        if ((err = pthread_cond_signal(&T->writers)) != 0)
            syserr ("cond signal failed", err);
    }
    if ((err = pthread_mutex_unlock(&T->mutex)) != 0) {
        syserr ("mutex failed", err);
    }
}

// funkcja "odblokowująca" ścieżkę od wierzchołka tree do korzenia
// wypuszcza pisarza/czytelnika w zależności od wartości writer z danego wierzchołka
// następnie wypuszcza czytelników od rodzica danego wierzchołka do korzenia
static void unlock_path(Tree *tree, bool writer) {
    if (tree == NULL) return;

    Tree *prev_tree = tree;
    Tree *curr_tree = tree->ancestor;

    if (writer) finish_writing(prev_tree);
    else finish_reading(prev_tree);

    prev_tree = curr_tree;
    if (curr_tree != NULL) curr_tree = curr_tree->ancestor;

    while (prev_tree != NULL) {
        finish_reading(prev_tree);
        prev_tree = curr_tree;
        if (curr_tree != NULL) curr_tree = curr_tree->ancestor;
    }
}

// funkcja "blokująca" dostęp do wierzchołka
// na ścieżce do danego wierzchołka umieszcza czytelników, w danym wierzchołku natomiast
// czytelnika lub pisarza w zależnosći od wartości writer
// funkcja zwraca ostatni wierzchołek, na którym udało się umieścić pisarza
// (potem na przykład trafiliśmy na nieistniejący folder)
// zmienna success informuje, czy udało się dojść do końca funkcji (jest potrzebna w tree_move)
static Tree *lock_path(Tree *tree, const char *path, bool writer, bool *success) {
    if (tree == NULL) return NULL;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subfolder_path = path;

    Tree *last_locked = tree;
    Tree *curr_tree = tree;
    HashMap *curr_map = tree->contents;
    while ((subfolder_path = split_path(subfolder_path, component))) {
        start_reading(curr_tree);

        curr_tree = (Tree *)hmap_get(curr_map, component);
        if (curr_tree == NULL) return last_locked; //jeśli nie doszliśmy do końca nie umieściliśmy pisarza
        else last_locked = curr_tree;

        curr_map = curr_tree->contents;
    }

    if (writer) start_writing(curr_tree);
    else start_reading(curr_tree);

    *success = true;
    return last_locked;
}

// funkcja zwraca drzewo danej ścieżki lub NULL, jeśli nie istnieje
static Tree *get_folder_tree(Tree *tree, const char *path) {
    if (tree == NULL) return NULL;

    char component[MAX_FOLDER_NAME_LENGTH + 1];
    const char* subfolder_path = path;

    Tree *curr_tree = tree;
    HashMap *result_map = tree->contents;
    while ((subfolder_path = split_path(subfolder_path, component))) {

        curr_tree = (Tree *)hmap_get(result_map, component);
        if (curr_tree == NULL) return NULL;
        result_map = curr_tree->contents;
    }
    return curr_tree;
}

// sprawdza czy drzewo jest nullem
// jeśli jest zwraca false i odblokuje ścieżkę od wierzchołka tree do korzenia
static bool check_tree_and_unlock(Tree *tree, Tree *last_locked, bool success) {
    if (tree == NULL) {
        if (success) unlock_path(last_locked, true);
        else unlock_path(last_locked, false);
        return false;
    }
    return true;
}

// zwraca poddrzewo danej ścieżki lub NULL jeśli to poddrzewo nie istnieje
static Tree *get_parent_tree(Tree *tree, const char *path, Tree *last_locked, bool success, char *component) {
    char *path_to_parent = make_path_to_parent(path, component);
    Tree *parent_tree = get_folder_tree(tree, path_to_parent);
    if (path_to_parent != NULL) free(path_to_parent);

    if (!check_tree_and_unlock(parent_tree, last_locked, success)) return NULL;
    else return parent_tree;
}

static Tree *get_and_lock_parent(Tree *tree, const char *path, char *component, Tree **last_locked) {
    bool success = false;
    char *path_to_parent = make_path_to_parent(path, component);

    *last_locked = lock_path(tree, path_to_parent, true, &success);
    Tree *parent_tree = get_folder_tree(tree, path_to_parent);

    if (path_to_parent != NULL) free(path_to_parent);
    if (parent_tree == NULL) {
        unlock_path(*last_locked, false);
        return NULL;
    }
    return parent_tree;
}

static Tree *lock_lca(Tree* tree, const char* source, const char* target, bool *success) {
    char *lca = last_common_ancestor(source, target);
    Tree *last_locked = lock_path(tree, lca, true, success);
    free(lca);
    return last_locked;
}

static bool check_source_and_target(const char* source, const char* target, int *ret_val) {
    if (!is_path_valid(source) || !is_path_valid(target)) {
        *ret_val = EINVAL;
        return false;
    }
    if (strcmp(source, "/") == 0) {
        *ret_val = EBUSY;
        return false;
    }
    if (strcmp(target, "/") == 0) {
        *ret_val = EEXIST;
        return false;
    }
    if (is_a_prefix_of(source, target) && strcmp(target, source) != 0) {
        *ret_val = MY_ERROR;
        return false;
    }
    return true;
}

Tree* tree_new() {
    int err;
    Tree *T = malloc(sizeof (Tree));
    if (T == NULL) fatal("memory allocation failed");

    T->contents = hmap_new();
    T->ancestor = NULL;

    if ((err = pthread_mutex_init(&T->mutex, 0)) != 0)
        syserr ("mutex init failed", err);
    if ((err = pthread_cond_init(&T->readers, 0)) != 0)
        syserr ("cond init 1 failed", err);
    if ((err = pthread_cond_init(&T->writers, 0)) != 0)
        syserr ("cond init 2 failed", err);

    T->readers_number = 0;
    T->writers_number = 0;
    T->waiting_readers = 0;
    T->waiting_writers = 0;
    T->whose_turn = 2;

    return T;
}

void tree_free(Tree *tree) {
    int err;
    if (tree->contents != NULL) {
        HashMapIterator it = hmap_iterator(tree->contents);
        const char *key = NULL;
        void *value = NULL;

        while (hmap_next(tree->contents, &it, &key, &value)) {
            tree_free((Tree *)value);
        }
        hmap_free(tree->contents);
    }

    if ((err = pthread_cond_destroy (&tree->readers)) != 0)
        syserr ("cond destroy 1 failed", err);
    if ((err = pthread_cond_destroy (&tree->writers)) != 0)
        syserr ("cond destroy 2 failed", err);
    if ((err = pthread_mutex_destroy (&tree->mutex)) != 0)
        syserr ("mutex destroy failed", err);

    free(tree);
}

char* tree_list(Tree *tree, const char *path) {
    if (!is_path_valid(path)) return NULL;
    bool success = false;

    Tree *last_locked = lock_path(tree, path, false, &success);
    Tree *result_tree = get_folder_tree(tree, path);

    if (result_tree == NULL) {
        unlock_path(last_locked, false);
        return NULL;
    }
    else {
        char *result = make_map_contents_string(result_tree->contents);
        unlock_path(last_locked, false);
        return result;
    }
}

int tree_create(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return EINVAL;
    if (strcmp(path, "/") == 0) return EEXIST;

    char new_folder[MAX_FOLDER_NAME_LENGTH + 1];
    Tree *last_locked;
    Tree *parent_tree = get_and_lock_parent(tree, path, new_folder, &last_locked);

    if (parent_tree == NULL) return ENOENT;

    Tree *created = tree_new();
    if (hmap_insert(parent_tree->contents, new_folder, created)) {
        created->ancestor = parent_tree;
        unlock_path(last_locked, true);
        return 0;
    }
    else {
        tree_free(created);
        unlock_path(last_locked, true);
        return EEXIST;
    }
}

int tree_remove(Tree* tree, const char* path) {
    if (!is_path_valid(path)) return EINVAL;
    if (strcmp(path, "/") == 0) return EBUSY;

    char to_remove[MAX_FOLDER_NAME_LENGTH + 1];
    Tree *last_locked;
    Tree *parent_tree = get_and_lock_parent(tree, path, to_remove, &last_locked);

    if (parent_tree == NULL) return ENOENT;

    Tree *tree_to_remove = (Tree *) hmap_get(parent_tree->contents, to_remove);
    if (tree_to_remove == NULL) {
        unlock_path(last_locked, true);
        return ENOENT;
    }
    HashMap *map_to_remove = tree_to_remove->contents;

    if (hmap_size(map_to_remove) != 0) {
        unlock_path(last_locked, true);
        return ENOTEMPTY;
    }
    else {
        hmap_remove(parent_tree->contents, to_remove);
        tree_free(tree_to_remove);
        unlock_path(last_locked, true);
        return 0;
    }
}

// w tree_move blokujemy ścieżkę do ostatniego wspólnego przodka (lca)
// jest to wygodny sposób zapewnienia operacji bezpieczeństwa (w obrębie gdzie przenosimy folder source do target
// żaden inny wątek nie może nic zmienić)
int tree_move(Tree* tree, const char* source, const char* target) {
    int ret_val = 0;
    if (!check_source_and_target(source, target, &ret_val)) return  ret_val;

    bool success = false;
    char folder_to_create[MAX_FOLDER_NAME_LENGTH + 1];
    char folder_to_remove[MAX_FOLDER_NAME_LENGTH + 1];

    Tree *last_locked = lock_lca(tree, source, target, &success);

    Tree *ancestor_s = get_parent_tree(tree, source, last_locked, success, folder_to_remove);
    if (ancestor_s == NULL) return ENOENT;

    Tree *tree_to_move = (Tree *)hmap_get(ancestor_s->contents, folder_to_remove);
    if (!check_tree_and_unlock(tree_to_move, last_locked, success)) return ENOENT;

    Tree *ancestor_t = get_parent_tree(tree, target, last_locked, success, folder_to_create);
    if (ancestor_t == NULL) return ENOENT;

    if (strcmp(source, target) == 0) {
        unlock_path(last_locked, true);
        return 0;
    }
    if (!hmap_insert(ancestor_t->contents, folder_to_create, tree_to_move)) {
        unlock_path(last_locked, true);
        return EEXIST;
    }
    tree_to_move->ancestor = ancestor_t;
    hmap_remove(ancestor_s->contents, folder_to_remove);
    unlock_path(last_locked, true);
    return 0;
}