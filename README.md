# Concurrent-folder-tree-simulator

University task for concurrent programming course.
The implementation of Hashmap and some path ustils functions were given for this task.

## Task description

The task is to implement a part of a file system, specifically a concurrent data structure representing a tree of folders. Paths are represented by strings in the form "/foo/bar/baz/". You need to define the Tree structure and the following operations:

Tree* tree_new()
Creates a new tree of folders with one empty folder "/".

void tree_free(Tree*)
Frees all memory associated with the given tree.

char* tree_list(Tree* tree, const char* path)
Lists the contents of the given folder, returning a new string in the form "foo,bar,baz" (all names of subfolders; only immediate subfolders, without going deeper; in any order, separated by commas, terminated by a null character). (Freeing the memory of the string is the responsibility of the caller of tree_list).

int tree_create(Tree* tree, const char* path)
Creates a new subfolder (e.g., for path="/foo/bar/baz/", creates an empty subfolder baz in the folder "/foo/bar/").

int tree_remove(Tree* tree, const char* path)
Removes the folder if it is empty.

int tree_move(Tree* tree, const char* source, const char* target)
Moves the source folder along with its contents to the target location (the entire subtree is moved), if possible.

The list, create, remove, and move operations must be atomic (i.e., the returned values should be as if the operations were executed sequentially in some order). If two operations are called in parallel (i.e., not one started after the other ended), their returned values may be as if they were executed in any order: it is OK if the chosen order leads to an error code being returned.

## Opis zadania

Zadanie polega na zaimplementowaniu części systemu plików, a konkretnie współbieżnej struktury danych reprezentującej drzewo folderów.
Ścieżki są reprezentowane napisami postaci "/foo/bar/baz/".
Należy zdefiniować strukturę Tree oraz następujące operacje:

Tree* tree_new()
Tworzy nowe drzewo folderów z jednym, pustym folderem "/".

void tree_free(Tree*)
Zwalnia całą pamięć związaną z podanym drzewem.

char* tree_list(Tree* tree, const char* path)
Wymienia zawartość danego folderu, zwracając nowy napis postaci "foo,bar,baz" (wszystkie nazwy podfolderów; tylko bezpośrednich podfolderów, czyli bez wchodzenia wgłąb; w dowolnej kolejności, oddzielone przecinkami, zakończone znakiem zerowym). (Zwolnienie pamięci napisu jest odpowiedzialnością wołającego tree_list).

int tree_create(Tree* tree, const char* path)
Tworzy nowy podfolder (np. dla path="/foo/bar/baz/", tworzy pusty podfolder baz w folderze "/foo/bar/").

int tree_remove(Tree* tree, const char* path)
Usuwa folder, o ile jest pusty.

int tree_move(Tree* tree, const char* source, const char* target)
Przenosi folder source wraz z zawartością na miejsce target (przenoszone jest całe poddrzewo), o ile to możliwe.

Operacje list, create, remove i move muszą być atomowe (tzn. zwracane wartości powinny być takie jak gdyby operacje wykonały się sekwencyjnie w jakiejś kolejności).
Jeśli dwie operacje zostały wywołane równolegle (tj. nie jedna rozpoczęta po zakończeniu drugiej), to ich zwracane wartości mogą być takie jak gdyby wykonały się w dowolnej kolejności: jest OK jeśli wybrana kolejność prowadzi do zwrócenia kodu błędu.
