# Concurrent-folder-tree-simulator

This is a project task for "Concurrent Programming" course at University of Warsaw, the Faculty of Mathematics, Informatics and Mechanics, 2021/2022. I do not own the idea for this project.

The implementation of Hashmap and some path ustils functions were given for this task.

## Project description

This project is a simulation of a type of a small file system - concurrent data structure representing a tree of folders. Paths are represented by strings in the form "/a/b/c/". The following operations can be called on the Tree structure:

Tree* tree_new()  
Creates a new tree with an empty folder "/".  

void tree_free(Tree*)  
Frees all memory the given tree was using.  

char* tree_list(Tree* tree, const char* path)
Lists the contents of the given folder.

int tree_create(Tree* tree, const char* path)
Creates a new, empty subfolder.

int tree_remove(Tree* tree, const char* path)
Removes the folder if it is empty.

int tree_move(Tree* tree, const char* source, const char* target)
Moves the source folder with its contents to the target location (the entire subtree is moved), if possible.

