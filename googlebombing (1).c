/*
 * Projet 7 - Simulation d'un Google Bombing
 *
 * Version finale:
 * - stockage creux CSR: adapte aux gros graphes du TP;
 * - lecture de trois formats:
 *     1) Harvard500: "n n m" puis m arcs "i j";
 *     2) matrices creuses: "n m" puis lignes "i deg j1 w1 ...";
 *     3) Matrix Market .mtx: "rows cols nnz" puis arcs "i j [poids]";
 * - PageRank par iteration de puissance avec gestion des noeuds dangling;
 * - attaque sans recopier tout le graphe: on superpose les attaquants pendant
 *   le produit matrice-vecteur, ce qui evite une grosse consommation memoire;
 * - balayage optionnel sur alpha pour etudier l'influence du facteur de
 *   teleportation (parametre demande dans l'introduction du sujet);
 * - sous-echantillonnage des valeurs de k pour rester rapide sur les
 *   gros graphes tout en gardant une courbe lisible.
 *
 * Compilation:
 *   gcc googlebombing_final.c -O2 -Wall -Wextra -o googlebombing_final.exe
 *
 * Usage:
 *   googlebombing_final.exe [options] <fichier> <alpha> <k_max> <sortie.csv>
 *
 * Options:
 *   --alpha-sweep "0.50,0.70,0.85,0.95" : lance plusieurs alpha (alpha argv ignore)
 *   --prof-k                             : k = 1,2,5,10,20,50,100,200 (bornes par k_max)
 *   --k-step <n>                         : ne teste que k = 1, 1+n, 1+2n, ..., k_max
 *   --quiet                              : moins de logs
 *
 * Mettre k_max=0 ne fait que verifier la lecture du graphe.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define EPS 1e-6     /* seuil de convergence L1 pour l'iteration de puissance */
#define MAX_IT 1000   /* nombre maximum d'iterations PageRank avant arret force */
#define MAX_ALPHAS 32 /* nombre maximum de valeurs d'alpha dans un alpha-sweep  */

typedef enum
{
    FORMAT_EDGE_LIST,
    FORMAT_WEIGHTED_ROWS,
    FORMAT_MATRIX_MARKET
} InputFormat;

typedef enum
{
    ATTACK_ISOLATED = 0, /* chaque attaquant pointe uniquement vers la cible */
    ATTACK_COMPLETE = 1, /* graphe complet entre attaquants + cible           */
    ATTACK_RING = 2      /* anneau: cible -> a0 -> a1 -> ... -> ak -> cible   */
} AttackType;

typedef enum
{
    TARGET_FORTE = 0,  /* noeud de PageRank maximal  */
    TARGET_MOYENNE = 1,/* noeud median               */
    TARGET_FAIBLE = 2  /* noeud de PageRank minimal  */
} TargetKind;

/* Graphe oriente stocke au format CSR (Compressed Sparse Row).
 * row_ptr[i]..row_ptr[i+1]-1 donne les arcs sortants du noeud i.
 * val[p] est le poids stochastique de l'arc col_idx[p] (deja normalise
 * par la somme des poids sortants de la ligne). */
typedef struct
{
    int n;       /* nombre de noeuds                */
    int m;       /* nombre d'arcs                   */
    int *row_ptr;/* tableau de pointeurs de lignes (taille n+1) */
    int *col_idx;/* colonnes des arcs (taille m)    */
    double *val; /* poids stochastiques (taille m)  */
} Graph;

typedef struct
{
    int node;
    double score;
} RankedNode;

typedef struct
{
    double score;     /* PageRank de la cible apres convergence */
    int iterations;   /* nombre d'iterations effectuees         */
    double seconds;   /* temps CPU en secondes                  */
} PageRankRun;

typedef struct
{
    int verbose;
    int k_step;
    int professor_k;
    int n_alphas;
    double alphas[MAX_ALPHAS];
} RunOptions;

/* Arc brut avant construction CSR (format intermediaire de lecture MTX). */
typedef struct
{
    int src;
    int dst;
    double weight;
} RawEdge;

/* --- Utilitaires memoire -------------------------------------------------- */

/* Alloue n*size octets initialises a zero; arrete le programme si echec. */
static void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n, size);
    if (!p)
    {
        fprintf(stderr, "Erreur allocation memoire\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* Alloue size octets non initialises; arrete le programme si echec. */
static void *xmalloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
    {
        fprintf(stderr, "Erreur allocation memoire\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

/* --- Lecture et detection de format --------------------------------------- */

/* Compte les valeurs numeriques consecutives sur une ligne de texte.
 * Sert a distinguer "n n m" (edge-list) de "n m" (matrice creuse). */
static int count_numbers(const char *line)
{
    int count = 0;
    const char *p = line;
    char *end = NULL;

    while (*p)
    {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        strtod(p, &end);
        if (end == p)
            break;
        count++;
        p = end;
    }
    return count;
}

/* Detecte le format du fichier en lisant uniquement la premiere ligne.
 * Retourne FORMAT_MATRIX_MARKET si le commentaire MatrixMarket est present,
 * FORMAT_EDGE_LIST si la ligne contient 3 entiers (n n m), sinon FORMAT_WEIGHTED_ROWS. */
static InputFormat detect_format(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Erreur ouverture fichier %s\n", filename);
        exit(EXIT_FAILURE);
    }

    char line[4096];
    if (!fgets(line, sizeof(line), f))
    {
        fprintf(stderr, "Erreur: fichier vide\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }
    fclose(f);

    if (strstr(line, "MatrixMarket") != NULL || strstr(line, "matrixmarket") != NULL)
    {
        return FORMAT_MATRIX_MARKET;
    }

    return (count_numbers(line) >= 3) ? FORMAT_EDGE_LIST : FORMAT_WEIGHTED_ROWS;
}

/* Libere les tableaux internes d'un graphe CSR et remet les champs a zero. */
static void free_graph(Graph *g)
{
    if (!g)
        return;
    free(g->row_ptr);
    free(g->col_idx);
    free(g->val);
    g->row_ptr = NULL;
    g->col_idx = NULL;
    g->val = NULL;
    g->n = 0;
    g->m = 0;
}

/* Agrandit le tableau counts jusqu'a pouvoir contenir l'indice needed_index.
 * Les nouvelles cases sont initialisees a zero.
 * Utilise un doublement de capacite pour amortir les reallocs. */
static void ensure_counts_capacity(int **counts, int *capacity, int needed_index)
{
    if (needed_index < *capacity)
        return;

    int old_capacity = *capacity;
    int new_capacity = old_capacity > 0 ? old_capacity : 1024;
    while (needed_index >= new_capacity)
    {
        if (new_capacity > 1073741823 / 2)
        {
            fprintf(stderr, "Erreur: trop de sommets\n");
            exit(EXIT_FAILURE);
        }
        new_capacity *= 2;
    }

    int *new_counts = realloc(*counts, (size_t)new_capacity * sizeof(int));
    if (!new_counts)
    {
        fprintf(stderr, "Erreur realloc\n");
        exit(EXIT_FAILURE);
    }
    for (int i = old_capacity; i < new_capacity; i++)
        new_counts[i] = 0;
    *counts = new_counts;
    *capacity = new_capacity;
}

/* Lit l'en-tete format edge-list: "n n m" (deux fois le nombre de noeuds
 * puis le nombre d'arcs). Arrete si le parsing echoue. */
static void read_edge_header(FILE *f, int *declared_n, int *declared_m)
{
    int n2;
    if (fscanf(f, "%d %d %d", declared_n, &n2, declared_m) != 3)
    {
        fprintf(stderr, "Erreur lecture en-tete edge-list\n");
        exit(EXIT_FAILURE);
    }
    if (*declared_n != n2)
    {
        fprintf(stderr, "Attention: en-tete non carree (%d, %d)\n", *declared_n, n2);
    }
}

/* Lit l'en-tete format matrice creuse: "n m" sur une ou deux lignes.
 * Supporte le cas ou n et m sont sur des lignes separees. */
static void read_weighted_header(FILE *f, int *declared_n, int *declared_m)
{
    char first[4096], second[4096];
    int nums_first;

    if (!fgets(first, sizeof(first), f))
    {
        fprintf(stderr, "Erreur lecture premiere ligne\n");
        exit(EXIT_FAILURE);
    }
    nums_first = count_numbers(first);

    if (nums_first >= 2)
    {
        if (sscanf(first, "%d %d", declared_n, declared_m) != 2)
        {
            fprintf(stderr, "Erreur lecture en-tete matrice creuse\n");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        if (sscanf(first, "%d", declared_n) != 1 || !fgets(second, sizeof(second), f) ||
            sscanf(second, "%d", declared_m) != 1)
        {
            fprintf(stderr, "Erreur lecture en-tete matrice creuse\n");
            exit(EXIT_FAILURE);
        }
    }
}

/* Construit un graphe CSR a partir d'un fichier Matrix Market (.mtx).
 * Gere les matrices symetriques (duplication des arcs), les poids optionnels
 * et la normalisation stochastique ligne par ligne. */
static Graph read_matrix_market_graph(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Erreur ouverture fichier %s\n", filename);
        exit(EXIT_FAILURE);
    }

    char line[4096];
    int symmetric = 0;
    int rows = 0, cols = 0, nnz = 0;

    /* Premiere ligne: banniere MatrixMarket (detecte la symetrie). */
    if (!fgets(line, sizeof(line), f))
    {
        fprintf(stderr, "Erreur: fichier MTX vide\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }
    if (strstr(line, "symmetric") != NULL || strstr(line, "SYMMETRIC") != NULL)
    {
        symmetric = 1;
    }

    /* Sauter les lignes de commentaires (commencant par '%'). */
    do
    {
        if (!fgets(line, sizeof(line), f))
        {
            fprintf(stderr, "Erreur: dimensions MTX introuvables\n");
            fclose(f);
            exit(EXIT_FAILURE);
        }
    } while (line[0] == '%');

    if (sscanf(line, "%d %d %d", &rows, &cols, &nnz) != 3)
    {
        fprintf(stderr, "Erreur: en-tete MTX invalide\n");
        fclose(f);
        exit(EXIT_FAILURE);
    }
    if (rows != cols)
    {
        fprintf(stderr, "Attention: matrice MTX non carree (%d, %d); n=max\n", rows, cols);
    }

    int n = rows > cols ? rows : cols;
    int capacity = nnz * (symmetric ? 2 : 1);
    RawEdge *edges = xmalloc((size_t)capacity * sizeof(RawEdge));
    int *row_counts = xcalloc((size_t)n, sizeof(int));
    double *row_sums = xcalloc((size_t)n, sizeof(double));
    int m = 0;

    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '%')
            continue;

        int src = 0, dst = 0;
        double weight = 1.0;
        int read = sscanf(line, "%d %d %lf", &src, &dst, &weight);
        if (read < 2)
            continue;
        if (src <= 0 || src > n || dst <= 0 || dst > n)
        {
            fprintf(stderr, "Erreur: arc MTX invalide %d -> %d (n=%d)\n", src, dst, n);
            fclose(f);
            free(edges);
            free(row_counts);
            free(row_sums);
            exit(EXIT_FAILURE);
        }
        if (weight < 0.0)
        {
            fprintf(stderr, "Erreur: poids MTX negatif sur %d -> %d\n", src, dst);
            fclose(f);
            free(edges);
            free(row_counts);
            free(row_sums);
            exit(EXIT_FAILURE);
        }

        /* Agrandissement dynamique du tableau d'arcs bruts si necessaire. */
        if (m >= capacity)
        {
            capacity = capacity > 0 ? capacity * 2 : 1024;
            RawEdge *tmp = realloc(edges, (size_t)capacity * sizeof(RawEdge));
            if (!tmp)
            {
                fprintf(stderr, "Erreur realloc MTX\n");
                fclose(f);
                free(edges);
                free(row_counts);
                free(row_sums);
                exit(EXIT_FAILURE);
            }
            edges = tmp;
        }
        edges[m].src = src - 1;
        edges[m].dst = dst - 1;
        edges[m].weight = weight;
        row_counts[src - 1]++;
        row_sums[src - 1] += weight;
        m++;

        /* Pour une matrice symetrique, on ajoute l'arc inverse (sauf boucles). */
        if (symmetric && src != dst)
        {
            if (m >= capacity)
            {
                capacity = capacity > 0 ? capacity * 2 : 1024;
                RawEdge *tmp = realloc(edges, (size_t)capacity * sizeof(RawEdge));
                if (!tmp)
                {
                    fprintf(stderr, "Erreur realloc MTX\n");
                    fclose(f);
                    free(edges);
                    free(row_counts);
                    free(row_sums);
                    exit(EXIT_FAILURE);
                }
                edges = tmp;
            }
            edges[m].src = dst - 1;
            edges[m].dst = src - 1;
            edges[m].weight = weight;
            row_counts[dst - 1]++;
            row_sums[dst - 1] += weight;
            m++;
        }
    }
    fclose(f);

    /* Construction du CSR: row_ptr puis placement des arcs normalises. */
    Graph g;
    g.n = n;
    g.m = m;
    g.row_ptr = xcalloc((size_t)g.n + 1, sizeof(int));
    g.col_idx = xmalloc((size_t)g.m * sizeof(int));
    g.val = xmalloc((size_t)g.m * sizeof(double));

    for (int i = 0; i < g.n; i++)
    {
        g.row_ptr[i + 1] = g.row_ptr[i] + row_counts[i];
    }

    int *cursor = xcalloc((size_t)g.n, sizeof(int));
    for (int e = 0; e < m; e++)
    {
        int src = edges[e].src;
        int pos = g.row_ptr[src] + cursor[src]++;
        g.col_idx[pos] = edges[e].dst;
        /* Normalisation: poids = w_ij / somme des poids sortants de i. */
        g.val[pos] = row_sums[src] > 0.0 ? edges[e].weight / row_sums[src] : 0.0;
    }

    if (nnz != 0 && !symmetric && nnz != m)
    {
        fprintf(stderr, "Attention: nnz MTX declare=%d, arcs lus=%d\n", nnz, m);
    }

    free(cursor);
    free(row_counts);
    free(row_sums);
    free(edges);
    return g;
}

/* Lit un graphe depuis un fichier texte (edge-list ou matrice creuse ponderee)
 * et construit la representation CSR en deux passes:
 *   1re passe: comptage des degres sortants pour dimensionner row_ptr;
 *   2e passe: remplissage de col_idx et val avec les poids normalises.
 * Pour l'edge-list, chaque arc a le poids uniforme 1/deg. */
static Graph read_graph(const char *filename)
{
    InputFormat fmt = detect_format(filename);
    if (fmt == FORMAT_MATRIX_MARKET)
    {
        return read_matrix_market_graph(filename);
    }

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Erreur ouverture fichier %s\n", filename);
        exit(EXIT_FAILURE);
    }

    int declared_n = 0, declared_m = 0;
    int max_id = 0;
    int edge_count = 0;
    int capacity = 0;
    int *row_counts = NULL;

    if (fmt == FORMAT_EDGE_LIST)
    {
        read_edge_header(f, &declared_n, &declared_m);
    }
    else
    {
        read_weighted_header(f, &declared_n, &declared_m);
    }

    ensure_counts_capacity(&row_counts, &capacity, declared_n + 1);
    max_id = declared_n;

    /* --- 1re passe: comptage des degres sortants --- */
    if (fmt == FORMAT_EDGE_LIST)
    {
        int src, dst;
        while (fscanf(f, "%d %d", &src, &dst) == 2)
        {
            if (src <= 0 || dst <= 0)
            {
                fprintf(stderr, "Erreur: indices invalides dans %s\n", filename);
                free(row_counts);
                fclose(f);
                exit(EXIT_FAILURE);
            }
            if (src > max_id)
                max_id = src;
            if (dst > max_id)
                max_id = dst;
            ensure_counts_capacity(&row_counts, &capacity, src);
            row_counts[src - 1]++;
            edge_count++;
        }
    }
    else
    {
        int src, deg;
        while (fscanf(f, "%d %d", &src, &deg) == 2)
        {
            if (src <= 0 || deg < 0)
            {
                fprintf(stderr, "Erreur: ligne invalide dans %s\n", filename);
                free(row_counts);
                fclose(f);
                exit(EXIT_FAILURE);
            }
            if (src > max_id)
                max_id = src;
            ensure_counts_capacity(&row_counts, &capacity, src);
            row_counts[src - 1] += deg;

            for (int e = 0; e < deg; e++)
            {
                int dst;
                double w;
                if (fscanf(f, "%d %lf", &dst, &w) != 2)
                {
                    fprintf(stderr, "Erreur: arc pondere invalide dans %s\n", filename);
                    free(row_counts);
                    fclose(f);
                    exit(EXIT_FAILURE);
                }
                if (dst <= 0)
                {
                    fprintf(stderr, "Erreur: destination invalide dans %s\n", filename);
                    free(row_counts);
                    fclose(f);
                    exit(EXIT_FAILURE);
                }
                if (dst > max_id)
                    max_id = dst;
                edge_count++;
            }
        }
    }
    fclose(f);

    if (max_id > capacity)
        ensure_counts_capacity(&row_counts, &capacity, max_id);

    /* Allocation du CSR a partir des comptages. */
    Graph g;
    g.n = max_id;
    g.m = edge_count;
    g.row_ptr = xcalloc((size_t)g.n + 1, sizeof(int));
    g.col_idx = xmalloc((size_t)g.m * sizeof(int));
    g.val = xmalloc((size_t)g.m * sizeof(double));

    for (int i = 0; i < g.n; i++)
    {
        g.row_ptr[i + 1] = g.row_ptr[i] + row_counts[i];
    }

    /* --- 2e passe: remplissage des colonnes et des poids --- */
    int *cursor = xcalloc((size_t)g.n, sizeof(int));
    f = fopen(filename, "r");
    if (!f)
    {
        fprintf(stderr, "Erreur reouverture fichier %s\n", filename);
        free(cursor);
        free(row_counts);
        free_graph(&g);
        exit(EXIT_FAILURE);
    }
    if (fmt == FORMAT_EDGE_LIST)
    {
        read_edge_header(f, &declared_n, &declared_m);
        int src, dst;
        while (fscanf(f, "%d %d", &src, &dst) == 2)
        {
            int row = src - 1;
            int pos = g.row_ptr[row] + cursor[row]++;
            g.col_idx[pos] = dst - 1;
        }
        /* Poids uniforme 1/deg pour l'edge-list (matrice stochastique). */
        for (int i = 0; i < g.n; i++)
        {
            int deg = g.row_ptr[i + 1] - g.row_ptr[i];
            for (int p = g.row_ptr[i]; p < g.row_ptr[i + 1]; p++)
            {
                g.val[p] = deg > 0 ? 1.0 / deg : 0.0;
            }
        }
    }
    else
    {
        read_weighted_header(f, &declared_n, &declared_m);
        int src, deg;
        while (fscanf(f, "%d %d", &src, &deg) == 2)
        {
            int row = src - 1;
            for (int e = 0; e < deg; e++)
            {
                int dst;
                double w;
                if (fscanf(f, "%d %lf", &dst, &w) != 2)
                {
                    fprintf(stderr, "Erreur seconde lecture dans %s\n", filename);
                    free(cursor);
                    free(row_counts);
                    free_graph(&g);
                    fclose(f);
                    exit(EXIT_FAILURE);
                }
                int pos = g.row_ptr[row] + cursor[row]++;
                g.col_idx[pos] = dst - 1;
                g.val[pos] = w;
            }
        }
    }
    fclose(f);

    free(cursor);
    free(row_counts);

    if (declared_m != 0 && declared_m != edge_count)
    {
        fprintf(stderr, "Attention: m declare=%d, arcs lus=%d\n", declared_m, edge_count);
    }
    if (declared_n != g.n)
    {
        fprintf(stderr, "Attention: n declare=%d, max indice lu=%d; n corrige a %d\n",
                declared_n, g.n, g.n);
    }

    return g;
}

/* --- PageRank ------------------------------------------------------------- */

/* Norme L1 de la difference entre deux vecteurs de taille n.
 * Utilisee comme critere d'arret de l'iteration de puissance. */
static double l1_diff(const double *a, const double *b, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; i++)
        s += fabs(a[i] - b[i]);
    return s;
}

/* Un pas du surfeur aleatoire sur le graphe de base (sans attaquants).
 * Formule: next[j] = alpha * sum_i(pi[i] * P[i][j])
 *                  + (1-alpha)/n + alpha * dangling/n
 * Les noeuds dangling (degre sortant nul) redistribuent leur masse
 * uniformement sur tous les noeuds (variante "dangling node handling"). */
static void multiply_base(const Graph *g, const double *pi, double alpha, double *next)
{
    double dangling = 0.0;
    for (int i = 0; i < g->n; i++)
        next[i] = 0.0;

    for (int i = 0; i < g->n; i++)
    {
        int begin = g->row_ptr[i];
        int end = g->row_ptr[i + 1];
        if (begin == end)
        {
            dangling += pi[i];
        }
        else
        {
            for (int p = begin; p < end; p++)
            {
                next[g->col_idx[p]] += alpha * pi[i] * g->val[p];
            }
        }
    }

    double uniform = (1.0 - alpha) / g->n + alpha * dangling / g->n;
    for (int i = 0; i < g->n; i++)
        next[i] += uniform;
}

/* Retourne le temps ecoule en secondes depuis start (horloge CPU). */
static double seconds_since(clock_t start)
{
    clock_t now = clock();
    return (double)(now - start) / CLOCKS_PER_SEC;
}

/* Calcule le PageRank du graphe de base par iteration de puissance.
 * Demarre d'une distribution uniforme et itere jusqu'a convergence L1 < EPS
 * ou MAX_IT iterations. Ecrit le vecteur converge dans pr (taille g->n). */
static PageRankRun pagerank_base(const Graph *g, double alpha, double *pr)
{
    double *cur = xmalloc((size_t)g->n * sizeof(double));
    double *next = xcalloc((size_t)g->n, sizeof(double));
    for (int i = 0; i < g->n; i++)
        cur[i] = 1.0 / g->n;

    clock_t t0 = clock();
    int iter = 0;
    double diff;
    do
    {
        multiply_base(g, cur, alpha, next);
        diff = l1_diff(cur, next, g->n);
        double *tmp = cur;
        cur = next;
        next = tmp;
        iter++;
    } while (diff > EPS && iter < MAX_IT);

    memcpy(pr, cur, (size_t)g->n * sizeof(double));
    free(cur);
    free(next);

    PageRankRun run = {0.0, iter, seconds_since(t0)};
    return run;
}

/*
 * Multiplication "augmentee" : produit matrice-vecteur du graphe etendu
 * (n base + k attaquants) sans materialiser la matrice etendue.
 *
 * Conventions:
 * - ATTACK_ISOLATED : chaque attaquant pointe uniquement vers la cible (1 arc).
 * - ATTACK_COMPLETE : chaque attaquant pointe vers les k-1 autres + la cible (k arcs).
 * - ATTACK_RING     : cycle target -> attaquant_0 -> ... -> attaquant_{k-1} -> target.
 *                     La cible conserve ses arcs existants ; l'arc supplementaire
 *                     vers attaquant_0 fait passer ses poids sortants de 1/deg a
 *                     1/(deg+1). Si la cible etait dangling, elle ne contribue
 *                     plus au surfeur aleatoire mais a attaquant_0 (1 arc).
 *
 * Le vecteur pi est de taille total_n = base_n + k:
 *   pi[0..base_n-1]          : noeuds du graphe original
 *   pi[base_n..total_n-1]    : attaquants
 */
static void multiply_attack(const Graph *g, const double *pi, double alpha,
                            AttackType type, int target, int k, double *next)
{
    int base_n = g->n;
    int total_n = base_n + k;
    int first_attacker = base_n;
    double dangling = 0.0;

    for (int i = 0; i < total_n; i++)
        next[i] = 0.0;

    /* Contribution des noeuds du graphe de base. */
    for (int i = 0; i < base_n; i++)
    {
        int begin = g->row_ptr[i];
        int end = g->row_ptr[i + 1];
        int deg = end - begin;

        if (type == ATTACK_RING && i == target)
        {
            /* La cible redirige une fraction de sa masse vers le premier attaquant
             * (nouvel arc sortant), en rescalant ses arcs existants en 1/(deg+1). */
            if (deg == 0)
            {
                next[first_attacker] += alpha * pi[i];
            }
            else
            {
                double old_scale = (double)deg / (double)(deg + 1);
                for (int p = begin; p < end; p++)
                {
                    next[g->col_idx[p]] += alpha * pi[i] * g->val[p] * old_scale;
                }
                next[first_attacker] += alpha * pi[i] / (double)(deg + 1);
            }
        }
        else if (deg == 0)
        {
            dangling += pi[i];
        }
        else
        {
            for (int p = begin; p < end; p++)
            {
                next[g->col_idx[p]] += alpha * pi[i] * g->val[p];
            }
        }
    }

    /* Contribution des noeuds attaquants selon le type d'attaque. */
    double attack_mass = 0.0;
    for (int a = 0; a < k; a++)
        attack_mass += pi[first_attacker + a];

    if (type == ATTACK_ISOLATED)
    {
        /* Tous les attaquants pointent vers la cible: toute la masse va a target. */
        next[target] += alpha * attack_mass;
    }
    else if (type == ATTACK_COMPLETE)
    {
        /* Graphe complet: chaque attaquant distribue egalement vers la cible
         * et les k-1 autres attaquants (poids 1/k par arc). */
        double w = 1.0 / k;
        next[target] += alpha * attack_mass * w;
        for (int a = 0; a < k; a++)
        {
            next[first_attacker + a] += alpha * (attack_mass - pi[first_attacker + a]) * w;
        }
    }
    else if (type == ATTACK_RING)
    {
        /* Anneau: a_i -> a_{i+1}, dernier attaquant -> cible. */
        for (int a = 0; a < k; a++)
        {
            int src = first_attacker + a;
            int dest = (a == k - 1) ? target : (src + 1);
            next[dest] += alpha * pi[src];
        }
    }

    /* Terme de teleportation (1-alpha) + redistribution de la masse dangling. */
    double uniform = (1.0 - alpha) / total_n + alpha * dangling / total_n;
    for (int i = 0; i < total_n; i++)
        next[i] += uniform;
}

/*
 * Initialisation "chaude" du vecteur de probabilite pour le graphe attaque.
 * Plutot que de partir d'une distribution uniforme stricte, on rescale
 * le PageRank de base sur les noeuds originaux et on attribue 1/total_n
 * aux nouveaux attaquants. Cela reduit le nombre d'iterations necessaires.
 */
static void warm_init(const double *pr_base, int base_n, int k, double *cur)
{
    int total_n = base_n + k;
    double scale = (double)base_n / (double)total_n;
    for (int i = 0; i < base_n; i++)
        cur[i] = pr_base[i] * scale;
    double share = 1.0 / (double)total_n;
    for (int a = 0; a < k; a++)
        cur[base_n + a] = share;
}

/* Calcule le PageRank du graphe etendu (base + k attaquants) par iteration de
 * puissance, en utilisant multiply_attack pour simuler la structure d'attaque
 * sans allouer de nouvelle matrice. Retourne le score de la cible apres
 * convergence ainsi que le nombre d'iterations et le temps CPU. */
static PageRankRun pagerank_attack(const Graph *g, double alpha, AttackType type,
                                   int target, int k, const double *pr_base)
{
    int total_n = g->n + k;
    double *cur = xmalloc((size_t)total_n * sizeof(double));
    double *next = xcalloc((size_t)total_n, sizeof(double));

    if (pr_base)
    {
        warm_init(pr_base, g->n, k, cur);
    }
    else
    {
        for (int i = 0; i < total_n; i++)
            cur[i] = 1.0 / total_n;
    }

    clock_t t0 = clock();
    int iter = 0;
    double diff;
    do
    {
        multiply_attack(g, cur, alpha, type, target, k, next);
        diff = l1_diff(cur, next, total_n);
        double *tmp = cur;
        cur = next;
        next = tmp;
        iter++;
    } while (diff > EPS && iter < MAX_IT);

    PageRankRun run = {cur[target], iter, seconds_since(t0)};
    free(cur);
    free(next);
    return run;
}

/* Comparateur descendant sur le score PageRank, pour qsort. */
static int compare_ranked_desc(const void *a, const void *b)
{
    const RankedNode *ra = (const RankedNode *)a;
    const RankedNode *rb = (const RankedNode *)b;
    if (ra->score < rb->score)
        return 1;
    if (ra->score > rb->score)
        return -1;
    return ra->node - rb->node;
}

static const char *attack_name(AttackType type)
{
    switch (type)
    {
    case ATTACK_ISOLATED:
        return "isoles";
    case ATTACK_COMPLETE:
        return "complet";
    case ATTACK_RING:
        return "anneau";
    default:
        return "inconnu";
    }
}

static const char *target_name(int i)
{
    switch (i)
    {
    case TARGET_FORTE:
        return "forte";
    case TARGET_MOYENNE:
        return "moyenne";
    case TARGET_FAIBLE:
        return "faible";
    default:
        return "inconnu";
    }
}

/* Selectionne trois cibles representant les extremes et la mediane du classement
 * PageRank: rang 1 (forte), rang n/2 (moyenne), rang n (faible).
 * Ecrit les indices de noeuds dans targets[] et leurs rangs dans ranks[]. */
static void choose_targets(const double *pr, int n, int targets[3], int ranks[3])
{
    RankedNode *ranking = xmalloc((size_t)n * sizeof(RankedNode));
    for (int i = 0; i < n; i++)
    {
        ranking[i].node = i;
        ranking[i].score = pr[i];
    }
    qsort(ranking, (size_t)n, sizeof(RankedNode), compare_ranked_desc);

    int pos[3] = {0, n / 2, n - 1};
    for (int i = 0; i < 3; i++)
    {
        targets[i] = ranking[pos[i]].node;
        ranks[i] = pos[i] + 1;
    }

    free(ranking);
}

/* Parse une liste d'alphas separes par des virgules ou des espaces.
 * Les valeurs hors de ]0,1[ sont ignorees avec un avertissement. */
static void parse_alpha_list(const char *s, RunOptions *opts)
{
    opts->n_alphas = 0;
    const char *p = s;
    while (*p)
    {
        while (*p && (*p == ',' || isspace((unsigned char)*p)))
            p++;
        if (!*p)
            break;
        char *end = NULL;
        double a = strtod(p, &end);
        if (end == p)
            break;
        if (a <= 0.0 || a >= 1.0)
        {
            fprintf(stderr, "Alpha %.4f hors de ]0,1[, ignore\n", a);
        }
        else if (opts->n_alphas < MAX_ALPHAS)
        {
            opts->alphas[opts->n_alphas++] = a;
        }
        p = end;
    }
}

/* Parse les options en ligne de commande et remplit opts.
 * Retourne 0 en succes, -1 si une option inconnue est rencontree.
 * *positional_start est positionne sur le premier argument non-option. */
static int parse_options(int argc, char **argv, RunOptions *opts,
                         int *positional_start)
{
    opts->verbose = 1;
    opts->k_step = 1;
    opts->professor_k = 0;
    opts->n_alphas = 0;

    int i = 1;
    while (i < argc)
    {
        const char *a = argv[i];
        if (strcmp(a, "--quiet") == 0)
        {
            opts->verbose = 0;
            i++;
        }
        else if (strcmp(a, "--prof-k") == 0)
        {
            opts->professor_k = 1;
            i++;
        }
        else if (strcmp(a, "--alpha-sweep") == 0 && i + 1 < argc)
        {
            parse_alpha_list(argv[i + 1], opts);
            i += 2;
        }
        else if (strcmp(a, "--k-step") == 0 && i + 1 < argc)
        {
            int s = atoi(argv[i + 1]);
            opts->k_step = s > 0 ? s : 1;
            i += 2;
        }
        else if (a[0] == '-' && a[1] == '-')
        {
            fprintf(stderr, "Option inconnue: %s\n", a);
            return -1;
        }
        else
        {
            break;
        }
    }
    *positional_start = i;
    return 0;
}

/* Lance les trois types d'attaque sur les trois cibles pour un k donne
 * et ecrit une ligne CSV par combinaison (9 lignes au total par appel). */
static void write_attack_rows_for_k(const Graph *g, double alpha, int k,
                                    const char *filename, FILE *csv,
                                    const double *pr, const int targets[3],
                                    const int ranks[3])
{
    AttackType attacks[3] = {ATTACK_ISOLATED, ATTACK_COMPLETE, ATTACK_RING};

    for (int c = 0; c < 3; c++)
    {
        int target = targets[c];
        for (int a = 0; a < 3; a++)
        {
            PageRankRun run = pagerank_attack(g, alpha, attacks[a], target,
                                              k, pr);
            double before = pr[target];
            double after = run.score;
            double ratio = before > 0.0 ? after / before : 0.0;

            fprintf(csv, "%s,%d,%d,%.6f,%d,%s,%d,%d,%.15f,%s,%.15f,%.15f,"
                         "%.6f,%d,%.4f\n",
                    filename, g->n, g->m, alpha, k, target_name(c),
                    target + 1, ranks[c], before, attack_name(attacks[a]),
                    after, after - before, ratio, run.iterations,
                    run.seconds);
        }
    }
}

/* Orchestre la simulation complete pour un alpha donne:
 * 1) calcul du PageRank de base;
 * 2) selection des trois cibles;
 * 3) boucle sur les valeurs de k (mode normal, --prof-k ou --k-step);
 * 4) ecriture de l'en-tete CSV si write_header != 0. */
static void run_simulation(const Graph *g, double alpha, int k_max,
                           const RunOptions *opts, const char *filename,
                           FILE *csv, int write_header)
{
    if (write_header)
    {
        fprintf(csv, "fichier,n,m,alpha,k,type_cible,page_cible,rank_initial,"
                     "pagerank_initial,type_attaque,pagerank_apres,augmentation,"
                     "ratio,iterations,temps_sec\n");
    }

    double *pr = xmalloc((size_t)g->n * sizeof(double));
    PageRankRun base = pagerank_base(g, alpha, pr);
    if (opts->verbose)
    {
        printf("[alpha=%.3f] PageRank initial: %d iters, %.3fs\n",
               alpha, base.iterations, base.seconds);
    }

    int targets[3], ranks[3];
    choose_targets(pr, g->n, targets, ranks);

    if (opts->verbose)
    {
        for (int i = 0; i < 3; i++)
        {
            printf("  cible %-8s: page %d, rang %d, PR=%.12f\n",
                   target_name(i), targets[i] + 1, ranks[i], pr[targets[i]]);
        }
    }

    if (opts->professor_k)
    {
        /* Mode --prof-k: liste fixe de valeurs de k pedagogiques. */
        int sizes[] = {1, 2, 5, 10, 20, 50, 100, 200};
        int nb_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
        int last_k = 0;
        for (int i = 0; i < nb_sizes; i++)
        {
            int k = sizes[i];
            if (k > k_max)
                break;
            write_attack_rows_for_k(g, alpha, k, filename, csv, pr, targets, ranks);
            last_k = k;
            if (opts->verbose)
                printf("  k=%d termine\n", k);
        }
        /* On s'assure que k_max est toujours teste, meme s'il n'est pas dans la liste. */
        if (last_k != k_max)
        {
            write_attack_rows_for_k(g, alpha, k_max, filename, csv, pr, targets, ranks);
            if (opts->verbose)
                printf("  k=%d termine\n", k_max);
        }
        free(pr);
        return;
    }

    /* Mode normal ou --k-step: on avance de k_step en k_step jusqu'a k_max. */
    int k = 1;
    while (k <= k_max)
    {
        write_attack_rows_for_k(g, alpha, k, filename, csv, pr, targets, ranks);
        if (opts->verbose)
            printf("  k=%d termine\n", k);

        if (opts->k_step <= 1 || k == k_max)
        {
            k++;
        }
        else
        {
            int next_k = k + opts->k_step;
            if (next_k > k_max && k != k_max)
            {
                k = k_max;
            }
            else
            {
                k = next_k;
            }
        }
    }

    free(pr);
}

int main(int argc, char **argv)
{
    RunOptions opts;
    int pos_start = 1;
    if (parse_options(argc, argv, &opts, &pos_start) < 0)
    {
        return EXIT_FAILURE;
    }

    int remaining = argc - pos_start;
    const char *filename = remaining >= 1 ? argv[pos_start + 0] : "data/Harvard500.txt";
    double alpha_arg = remaining >= 2 ? atof(argv[pos_start + 1]) : 0.85;
    int k_max = remaining >= 3 ? atoi(argv[pos_start + 2]) : 100;
    const char *csv_path = remaining >= 4 ? argv[pos_start + 3]
                                          : "results/resultats_final.csv";

    if (opts.n_alphas == 0)
    {
        if (alpha_arg <= 0.0 || alpha_arg >= 1.0)
        {
            fprintf(stderr, "Erreur: alpha doit etre dans ]0,1[ (recu %.3f)\n",
                    alpha_arg);
            return EXIT_FAILURE;
        }
        opts.alphas[opts.n_alphas++] = alpha_arg;
    }
    if (k_max < 0)
    {
        fprintf(stderr, "Erreur: k_max doit etre positif ou nul\n");
        return EXIT_FAILURE;
    }

    if (opts.verbose)
    {
        printf("=== Google Bombing - simulation ===\n");
        printf("Fichier  : %s\n", filename);
        if (opts.professor_k)
        {
            printf("k_max    : %d (mode prof: 1,2,5,10,20,50,100,200)\n", k_max);
        }
        else
        {
            printf("k_max    : %d (pas %d)\n", k_max, opts.k_step);
        }
        printf("alphas   :");
        for (int i = 0; i < opts.n_alphas; i++)
            printf(" %.3f", opts.alphas[i]);
        printf("\nSortie   : %s\n", csv_path);
    }

    Graph g = read_graph(filename);
    if (opts.verbose)
        printf("Graphe charge: n=%d, m=%d\n", g.n, g.m);

    if (k_max == 0)
    {
        if (opts.verbose)
            printf("Mode verification: lecture OK, pas de simulation.\n");
        free_graph(&g);
        return EXIT_SUCCESS;
    }

    FILE *csv = fopen(csv_path, "w");
    if (!csv)
    {
        fprintf(stderr, "Erreur creation %s\n", csv_path);
        free_graph(&g);
        return EXIT_FAILURE;
    }

    /* Lancement de la simulation pour chaque alpha (un seul si pas de sweep). */
    for (int i = 0; i < opts.n_alphas; i++)
    {
        run_simulation(&g, opts.alphas[i], k_max, &opts, filename, csv, i == 0);
    }

    fclose(csv);
    if (opts.verbose)
        printf("Resultats exportes dans %s\n", csv_path);

    free_graph(&g);
    return EXIT_SUCCESS;
}