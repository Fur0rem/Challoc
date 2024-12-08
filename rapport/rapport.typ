#import "conf.typ": conf
#import "utils.typ": bench_show, code_snippet

#let alloc_name = "challoc"

// set indentations
#set list(indent: 1cm)

#set page(
	margin: 1.5cm,
	header: [
		#set align(right + horizon)
		#emph[#alloc_name]
	], 
	footer: [
		#set align(right)
		#counter(page).display("1") / #locate(loc => {
			let lastpage = counter(page).final(loc)
			[#lastpage.at(0)]
		})
	],
	// fill: gradient.conic(..color.map.rainbow),
)

#show: conf.with(
	title: [
		Projet d'allocateur mémoire
	],
	project_name: [
		#alloc_name
	],
	authors: (
		(
			name: "BAUMANN Pierre",
			affiliation: "Master 1 CHPS - AISE",
		),
	),
	abstract: [Le projet d'AISE 2025 consiste à développer un allocateur mémoire pour le langage C, c'est à dire une bibliothèque possédant 4 fonctions : 
			#list(
				```c void* malloc(size_t size)```,
				```c void free(void *ptr)```,
				```c void* calloc(size_t nmemb, size_t size)```,
				```c void realloc(void *ptr, size_t size)```
			)
	]

)

= Introduction
Dans ce rapport, je vais d'abord présenter brièvement la structure du projet et les outils utilisés.
Ensuite, je montrerai l'allocateur mémoire minimal requis pour faire ces 4 fonctions.
Enfin, je présenterai toutes les fonctionnalités supplémentaires et optimisations que j'ai ajoutées, accompagnées de benchmarks pour les comparer à l'allocateur de base (libc).
Pour finir, je ferais un bilan de ce projet.

= Structure du projet
//Le projet a été réalisé en Rust, avec Cbindgen pour générer les headers C, et Criterion + Matplotlib pour les benchmarks et les plots.
//Le code est divisé en 3 parties
	//- L'allocateur : le code de l'allocateur mémoire
	//- Les tests : les tests unitaires
	//- Les benchmarks : les benchmarks pour comparer les performances de l'allocateur par rapport à libc

Le projet d'abord été réalisé en Rust, mais j'ai changé vers C en cours de route pour des raisons que je détaillerai à la fin.
Le code est divisé en 3 parties :
	- src/ -> L'allocateur : le code de l'allocateur mémoire
	- tests/ -> Les tests : les tests unitaires
	- benchmarks/ -> Les benchmarks : les benchmarks pour comparer les performances de l'allocateur par rapport à libc

Les tests principaux consistent à tester les 4 fonctions de l'allocateur, et vérifier qu'on puisse écrire et lire dans les blocs alloués, et que ces blocs ne se chevauchent pas.
Pour les benchmarks, je compare le temps d'exécution de malloc, calloc, realloc (tous accompagnés de free après) pour différentes tailles de mémoire demandée.

= Allocateur minimal
Pour l'implémentation vraiment basique, on ne fait que des appels à mmap et munmap pour allouer et libérer de la mémoire.
Cependant, munmap doit être appelé avec la taille de la mémoire allouée, donc on doit créer une structure de meta-données pour stocker cette taille.
On ajoute donc cette structure à la mémoire allouée, et on la lit lorsqu'on veut libérer la mémoire.
Pour realloc et calloc, on commence par les écrire de façon naïve avec juste malloc et free.
#set text(size: 9pt)
#code_snippet(code: "
// Code simplifié par souci de clarté.

// Structure pour les meta-données (on pourra ajouter d'autres informations plus tard)
struct AllocMetadata {
	size_t size;
}

void* malloc(size_t size) {
	// On ajoute de l'espace pour les meta-données
	size = size + sizeof(AllocMetadata);
	void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); // Appel à mmap
	// On vérifie que l'allocation a marché
	if (ptr == MAP_FAILED) {
		return NULL;
	}
	// On écrit les méta-données
	AllocMetadata* metadata = (AllocMetadata*)ptr;
	metadata->size = size;
	// On retourne le pointeur décalé pour l'utilisateur
	return ptr + sizeof(AllocMetadata);
}

void free(void* ptr) {
	// On récupère les méta-données pour libérer la mémoire
	AllocMetadata* metadata = (AllocMetadata*)(ptr - sizeof(AllocMetadata));
	// On libère la mémoire
	munmap(metadata, metadata->size);
}

void* calloc(size_t nmemb, size_t size) {
	// On alloue grâce à malloc
	void* ptr = malloc(nmemb * size);
	// On met tout à 0
	memset(ptr, 0, nmemb * size);
	// On retourne le pointeur
	return ptr;
}

void* realloc(void* ptr, size_t size) {
	// On alloue de la mémoire
	void* new_ptr = malloc(size);
	// On copie les données de l'ancien pointeur vers le nouveau
	AllocMetadata* metadata = (AllocMetadata*)(ptr - sizeof(AllocMetadata));
	memcpy(new_ptr, ptr, metadata->size - sizeof(AllocMetadata));
	// On libère l'ancien pointeur
	free(ptr);
	// On retourne le nouveau pointeur
	return new_ptr;
}", title: "Allocateur minimal", tabwidth: 4, lang: "c")
#set text(size: 11pt)

Avant de comparer à l'implémentation de la libc, je vais expliquer ma procédure de benchmarks



= Procédure de benchmarks

Pour les benchmarks, j'ai utilisé Criterion pour les faire tourner, puis matplotlib pour les plots (Pour l'instant aucune librairie Rust de plot est aussi flexible que matplotlib).
#footnote[Réalisés sur un CPU AMD Ryzen 5 5500U with Radeon Graphics, 6 Cores hyperthreadés à 2.1 GHz, 8 Go de RAM et NixOS 24.05]

Il y a 2 familles de benchmarks :
	- Les benchmarks unitaires de malloc, calloc, realloc pour différentes tailles de mémoire demandée.
	La taille allant de 1 octet à 1 Go, avec des paliers par puissance de 2.
	- Les benchmarks de programmes plus ou moins complexes en C pour voir l'impact de l'allocateur sur des programmes réels, car la performance de l'allocateur peut se ressentir aussi quand on écrit dans la mémoire allouée de façon intensive, et qu'il y a sûrement plein de mécanismes de cache qui rentrent en jeu.
		- small_allocs, big_allocs & mixed_allocs : Allouent plein de fois de la mémoire de taille petite (\<1024o), grande (\>16Mo), ou un mélange des 2.
		- zeroed_matrix_10x100000 & zeroed_matrix_100000x10 : Allouent une matrice de taille 10x100000, mais l'un alloue 10 lignes de 100000 éléments, et l'autre 100000 lignes de 10 éléments, tous initialisés à 0.
		- double_linked_list : Manipule une structure de liste doublement chaînée en insérant et supprimant des éléments à différents endroits de la liste, avec des Noeuds non-alignés sur 64 bits pour essayer de mettre les allocateurs à l'épreuve.
		- count_occurences : Compte le nombre d'occurences de chaque mot dans un recueil de poèmes un peu moins de 30000 lignes.
		- dijkstra : Génère un graphe de 600 noeuds pseudo-aléatoirement et calcule la distance de tous les noeuds à tous les autres neouds.
	
Je pense que les benchmarks sont assez exhaustifs, manipulant des structures simples comme des matrices à des structures plus complexes comme des listes doublement chaînées, des tas binaires, des hashmaps, des listes d'adjacence, etc.
Les pointeurs ont tous été marqués comme volatile pour éviter que le compilateur les optimise et que les benchmarks soient faussés.
De plus, j'ai aussi mesuré la consommation mémoire de chaque programme pour voir si l'allocateur consomme plus ou moins de mémoire que la libc.

Les plots seront présentés de cette façon:
	- La libc sera en bleu, et #alloc_name en rouge.
	- Les benchmarks unitaires seront présentés en courbe d'évolution du temps d'exécution en fonction de la taille de la mémoire demandée en échelle logarithmique (base 10 sur les ordonnées, base 2 sur les abscisses).
	- Les benchmarks de programmes réels seront présentés en barres, avec le temps d'exécution minimal, maximal et moyen, et la consommation mémoire pour un programme donné.



#pagebreak(weak: true)
= Comparaison avec la libc pour la version minimale
#linebreak()
#bench_show("bench_results/minimal")

Déjà, les courbes ont des allures similaires à celles montrées dans le cours, donc c'est dans un sens rassurant.

Comme on peut le voir sur les benchmarks unitaires et les programmes, l'allocateur minimal est bien plus lent et consomme plus de mémoire que la libc.

Le bottleneck le plus grand est pour les petites allocations, car mmap renvoit des pages de 4Ko, ce qui fait que 2 allocations de 32 octets prennent 8Ko, alors qu'on pourrait les mettre dans une seule page.
Cela se remarque car pour la courbe de malloc, il y a un plateau de 1o à 4Ko.
On peut aussi le voir car les programmes réels utilisant beaucoup de petites allocations ont une différence bien plus grande que les programmes utilisant des allocations plus grandes.

= Optimisations faites

= Fonctionnalités rajoutées

= Problèmes avec Rust
Malgré le fait que Rust soit mon langage préféré et avec lequel je suis le plus à l'aise, j'ai rencontré des problèmes qui m'ont fait repassé en C pour ce projet.
	- Beaucoup de boilerplate inutile pour pouvoir compiler le code puis l'interfacer avec du C. 
		Toutes les fonctions devaient être marquées 
		```rust
		#[no_mangle]
		pub unsafe extern "C" fn malloc(size: usize) -> *mut c_void {}
		```
		et il fallait rajouter des annotations #[repr(C)] pour les structures, ect... ainsi qu'utiliser l'intermédiaire de Cbindgen pour générer les headers C. 

	- Le système de test et de benchmarks ne marchait pas avec un allocateur custom interposé, car il rentrait en conflit avec l'allocateur de Rust. 
		Donc j'ai du ajouter des flags #[cfg(test)] et des features et d'autres macros de configuration pour pouvoir tester l'allocateur, ce qui rendait le code encore moins lisible et dupliquait les fichiers de configuration comme cbingen, Cargo.toml, etc... 
	
	- Criterion prennait énormément de ressources sur mon PC, ce qui faisait qu'il freezait pendant les benchmarks si en utilisant la configuration par défaut (beaucoup de samples répartis sur plusieurs threads, très long temps de warmup, etc...). 
		De plus, leur backend de rendu de courbes ne pouvait pas utiliser une échelle logarithmique autre qu'en base 10, alors que la base 2 est bien plus adaptée pour les benchmarks de mémoire. 
	
	- Impossibilité de faire tourner du code en sortie de programme. 
		Je voulais utiliser la fonction atexit pour afficher les fuites de mémoire en fin de programme, mais à chaque fois il y avait une erreur obscure de linker ```hidden symbol `atexit' in libc_nonshared.a(atexit.oS) is referenced by DSO, échec de l'édition de lien : bad value```.
		Je n'ai pas réussi à trouver de solution à ça, et disons que c'était le problème de trop pour moi. 
	
	- Il y a juste à voir l'allure de mon IDE pour se rendre compte que Rust n'était pas adapté pour ce projet. 
		#image("images/alloc_rust.png", fit: "contain") 
	
Et en vrai, j'aurais pu me douter que faire C $arrow.r$ Rust $arrow.r$ C était une mauvaise idée. 
Mais bon, j'ai quand même appris quelques trucs comme l'utilisation de Criterion, le développement \#![no_std], comment interfacer du Rust avec du C, et l'existence de quelques crates sympas, donc tout n'est pas perdu.
Je pense que Rust reste le meilleur langage pour tout ce qui est software, mais pour tout ce qui se rapproche plus de l'OS, vu qu'ils sont encore tous écrits en C, il vaut mieux rester sur ce langage malgré ses défauts et son manque d'outillage accessible dûs à son âge.

= Conclusion