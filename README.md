# Challoc

Challoc est un projet d'allocateur mémoire simple en C pour l'UE AISE du master CHPS de Paris-Saclay.
Il contient les fonctions suivantes avec la même signature que celles de la libc :
```c
void* challoc(size_t size);
void chafree(void* ptr);
void* charealloc(void* ptr, size_t size);
void* chacalloc(size_t nmemb, size_t size);
```

## Architecture du code

Le code est structuré de la manière suivante :
- `src/` contient les fichiers sources de l'allocateur mémoire.
- `docs/` contient la documentation du code.
- `tests/` pour les tests, vérifiant le bon fonctionnement de l'allocateur, de la détection de fuites mémoires, et de l'interposition des fonctions de la libc.
- `benchmarks/` pour les fichiers des benchmarks, des programmes C à utiliser pour comparer la performance par rapport à d'autres allocateurs, ainsi que leur résultats.
- `rapport/` contient les fichiers du rapport.
- `target/` contient les fichiers compilés.
- `Makefile` pour compiler le projet.
- `.clang-format et .clang-tidy` pour formatter et analyser le code.
- `Doxyfile` pour générer la documentation.
- `default.nix` pour les utilisateurs du gestionnaire de paquets Nix.

## Compilation

Tout est dans le Makefile :
```make libchalloc.so``` pour compiler la bibliothèque partagée avec interposition des fonctions de la libc
```make libchalloc_dev.so``` pour compiler la bibliothèque partagée sans interposition et avec des assertions en plus.
```make check``` pour lancer les tests.
```make benchmarks LABEL=<label>``` pour lancer les benchmarks temps et mémoire qui seront stockés dans le dossier `benchmarks/label/` avec le label spécifié puis mis en image dans le dossier `rapport/bench_results/label/`.
```make doc``` pour générer la documentation avec doxygen.
```make rapport``` pour générer le rapport fait en Typst.
```make clean``` pour nettoyer les fichiers compilés.

Lors de la compilation, la variable LEAKCHECK peut être définie (à 1, true, ou t) pour activer la vérification de fuites mémoires, ex : ```make libchalloc.so LEAKCHECK=true```.

## Dépendances

Un compilateur C, doxygen pour générer la documentation, clang-format et clang-tidy pour formatter et analyser le code, Typst pour générer le rapport, python et matplotlin pour générer les figures.

D'ailleurs, je vous invite à lire le rapport pour plus des informations sur le projet.
## Utilisation

Voici comment utiliser Challoc dans votre projet :

1. Incluez le fichier d'en-tête `<stdlib.h>` dans votre fichier source, ou le fichier d'en-tête `challoc.h` si souhaitez utiliser challoc sans interposition.
2. Compilez votre programme avec la bibliothèque partagée `libchalloc.so` ou `libchalloc_dev.so` (selon si vous souhaitez interposer les fonctions de la libc ou non).
3. Lancez votre programme avec LD_PRELOAD pour charger la bibliothèque partagée.
```bash
make libchalloc.so
LD_PRELOAD=./libchalloc.so ./votre_programme
```

## Implémentation

L'allocateur se base sur mmap.
Il utilise un vecteur de blocs alloués via mmap, et chaque bloc est utilisé en sous-allouant des blocs plus petits via une double liste chaînée en algorithmes first-fit.
Les blocs mmap complètement libérés sont stockés temporairement dans un vecteur de blocs libres afin de les réutiliser si possible.
L'allocateur possède aussi un petit allocateur en slab pour les petites allocations de 512 octets ou moins, la slab fait une taille totale de 4Mo (1 page), séparée en 1 cache de 512 octets, 2 caches de 256 octets, 4 caches de 128 octets, ect...

## Optimisations faites
- Segmentation en classes de tailles.
- Recyclage des blocs libérés.
- Coalescence des blocs libres.
- Gestion multi-thread avec des locks.
- Détection de fuites mémoires.

## Features
- Support en multithreading
- Détection de fuites mémoires
- Interposition des fonctions de la libc