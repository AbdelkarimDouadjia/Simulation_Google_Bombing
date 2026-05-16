/*
 * Google Bombing - Simulation complète
 * Usage : ./googlebombing <fichier.txt|fichier.mtx> [alpha] [k_max]
 *
 * Format .txt  (format original) :
 *   ligne 1 : n
 *   ligne 2 : m
 *   lignes  : <src> <deg> <dst1> <w1> <dst2> <w2> ...
 *
 * Format .mtx  (Matrix Market, AJOUT TD) :
 *   lignes % : commentaires (ignorés)
 *   ligne    : rows cols nnz
 *   lignes   : i j [val]   (1-based, val optionnelle)
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define EPS           1e-6
#define MAX_IT        200
#define K_MAX_COMPLET 3000

/* =========================================================
 *  Structure CSR  (inchangée)
 * ========================================================= */
typedef struct {
    int     n;
    int     m;
    int    *row_ptr;
    int    *col_idx;
    double *val;
} Graph;

/* =========================================================
 *  [AJOUT TD1 Q1] Matrice dense  (représentation pleine N×N)
 *  Utilisée pour vérification sur petits graphes.
 * ========================================================= */
typedef struct {
    int     n;
    double **data;   /* data[i][j] = proba de transition i -> j */
} DenseMatrix;

/* Alloue une matrice dense N×N initialisée à 0 */
DenseMatrix dense_alloc(int n) {
    DenseMatrix d;
    d.n    = n;
    d.data = malloc(n * sizeof(double *));
    for (int i = 0; i < n; i++) {
        d.data[i] = calloc(n, sizeof(double));
    }
    return d;
}

/* Libère une matrice dense */
void dense_free(DenseMatrix *d) {
    for (int i = 0; i < d->n; i++) free(d->data[i]);
    free(d->data);
    d->data = NULL;
}

/*
 * Construit la matrice Google dense à partir du graphe CSR.
 * G = alpha * P + (1-alpha)/n * E  avec gestion des dangling nodes.
 *   P[i][j] = val si arc i->j, colonne uniforme 1/n si noeud sans sortie.
 */
DenseMatrix csr_to_dense_google(const Graph *g, double alpha) {
    int n = g->n;
    DenseMatrix d = dense_alloc(n);

    for (int i = 0; i < n; i++) {
        int deg = g->row_ptr[i+1] - g->row_ptr[i];
        if (deg == 0) {
            /* dangling : surfer téléporte uniformément */
            for (int j = 0; j < n; j++)
                d.data[i][j] = 1.0 / n;
        } else {
            for (int k = g->row_ptr[i]; k < g->row_ptr[i+1]; k++)
                d.data[i][g->col_idx[k]] = alpha * g->val[k];
            for (int j = 0; j < n; j++)
                d.data[i][j] += (1.0 - alpha) / n;
        }
    }
    return d;
}

/*
 * Affiche la matrice dense (pour petits graphes, n <= 20 conseillé).
 */
void dense_afficher(const DenseMatrix *d) {
    printf("Matrice Google dense (%d x %d) :\n", d->n, d->n);
    for (int i = 0; i < d->n; i++) {
        printf("  [");
        for (int j = 0; j < d->n; j++)
            printf(" %6.4f", d->data[i][j]);
        printf(" ]\n");
    }
}

/*
 * [AJOUT TD1 Q1] Produit vecteur × matrice dense  O(N²)
 * res[j] = sum_i pi[i] * G[i][j]
 */
void mult_vect_mat_dense(const double *pi, const DenseMatrix *d, double *res) {
    int n = d->n;
    for (int j = 0; j < n; j++) {
        res[j] = 0.0;
        for (int i = 0; i < n; i++)
            res[j] += pi[i] * d->data[i][j];
    }
}

/* =========================================================
 *  Prototypes
 * ========================================================= */
double *pagerank(const Graph *g, double alpha, int verbose);
Graph attaque_isoles(const Graph *g, int k, int target);
Graph attaque_complet(const Graph *g, int k, int target);
Graph attaque_anneau(const Graph *g, int k, int target);

/* =========================================================
 *  Utilitaires  (inchangés)
 * ========================================================= */
double norme_L1(const double *a, const double *b, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += fabs(a[i] - b[i]);
    return s;
}

void free_graph(Graph *g) {
    free(g->row_ptr);
    free(g->col_idx);
    free(g->val);
    g->row_ptr = NULL;
    g->col_idx = NULL;
    g->val     = NULL;
}

/* =========================================================
 *  Lecture du graphe — format .txt original  (inchangée)
 * ========================================================= */
Graph lire_graphe_txt(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", filename);
        exit(1);
    }

    Graph g;
    if (fscanf(f, "%d", &g.n) != 1) { fprintf(stderr, "Erreur lecture n\n"); exit(1); }
    if (fscanf(f, "%d", &g.m) != 1) { fprintf(stderr, "Erreur lecture m\n"); exit(1); }

    g.row_ptr = calloc(g.n + 1, sizeof(int));
    g.col_idx = malloc(g.m * sizeof(int));
    g.val     = malloc(g.m * sizeof(double));

    fprintf(stderr, "Allocation OK: %.1f Mo\n",
        (g.m * (double)(sizeof(int) + sizeof(double)) + g.n * sizeof(int)) / 1e6);
    if (!g.row_ptr || !g.col_idx || !g.val) { fprintf(stderr, "Erreur allocation\n"); exit(1); }

    int pos = 0;
    g.row_ptr[0] = 0;
    for (int i = 0; i < g.n; i++) {
        int src, deg;
        if (fscanf(f, "%d %d", &src, &deg) != 2) {
            fprintf(stderr, "Erreur lecture noeud %d\n", i); exit(1);
        }
        for (int j = 0; j < deg; j++) {
            int    dst;
            double w;
            if (fscanf(f, "%d %lf", &dst, &w) != 2) {
                fprintf(stderr, "Erreur lecture arc noeud %d\n", i); exit(1);
            }
            if (dst < 1 || dst > g.n) {
                fprintf(stderr, "Arc invalide: src=%d dst=%d (n=%d)\n", i+1, dst, g.n);
                exit(1);
            }
            g.col_idx[pos] = dst - 1;
            g.val[pos]     = w;
            pos++;
        }
        g.row_ptr[i + 1] = pos;
    }
    fclose(f);
    g.m = pos;
    return g;
}

/* =========================================================
 *  [AJOUT TD] Lecture du graphe — format Matrix Market (.mtx)
 *
 *  Gère :
 *    - lignes de commentaires commençant par '%'
 *    - en-tête "rows cols nnz"
 *    - arcs "i j [val]"  (indices 1-based → 0-based)
 *    - poids uniforme 1/deg si pas de valeur dans le fichier
 * ========================================================= */
Graph lire_graphe_mtx(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", filename);
        exit(1);
    }

    /* Sauter les lignes de commentaires % */
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != '%') break;
    }

    /* L'en-tête est dans `line` : rows cols nnz */
    int rows, cols, nnz;
    if (sscanf(line, "%d %d %d", &rows, &cols, &nnz) != 3) {
        fprintf(stderr, "Erreur: en-tête MTX invalide\n");
        exit(1);
    }

    int n = rows;   /* graphe carré */

    /* Lecture brute des arcs (1-based) */
    int    *src_arr = malloc(nnz * sizeof(int));
    int    *dst_arr = malloc(nnz * sizeof(int));
    double *w_arr   = malloc(nnz * sizeof(double));
    if (!src_arr || !dst_arr || !w_arr) { fprintf(stderr, "Erreur alloc MTX\n"); exit(1); }

    int *deg_out = calloc(n, sizeof(int));

    int pos = 0;
    while (pos < nnz && fgets(line, sizeof(line), f)) {
        if (line[0] == '%') continue;
        int i, j;
        double v = 1.0;
        int r = sscanf(line, "%d %d %lf", &i, &j, &v);
        if (r < 2) continue;
        if (i < 1 || i > n || j < 1 || j > n) {
            fprintf(stderr, "Arc MTX invalide: %d %d (n=%d)\n", i, j, n);
            exit(1);
        }
        src_arr[pos] = i - 1;
        dst_arr[pos] = j - 1;
        w_arr[pos]   = v;
        deg_out[i - 1]++;
        pos++;
    }
    int m = pos;
    fclose(f);

    /* Normaliser les poids par le degré sortant (si val = 1.0 partout) */
    for (int k = 0; k < m; k++) {
        int s = src_arr[k];
        if (deg_out[s] > 0)
            w_arr[k] = 1.0 / deg_out[s];
    }

    /* Construction CSR */
    Graph g;
    g.n       = n;
    g.m       = m;
    g.row_ptr = calloc(n + 1, sizeof(int));
    g.col_idx = malloc(m * sizeof(int));
    g.val     = malloc(m * sizeof(double));

    /* Compte des arcs par ligne (source) */
    for (int k = 0; k < m; k++) g.row_ptr[src_arr[k] + 1]++;
    for (int i = 0; i < n; i++) g.row_ptr[i+1] += g.row_ptr[i];

    int *cursor = calloc(n, sizeof(int));
    for (int k = 0; k < m; k++) {
        int s = src_arr[k];
        int p = g.row_ptr[s] + cursor[s];
        g.col_idx[p] = dst_arr[k];
        g.val[p]     = w_arr[k];
        cursor[s]++;
    }

    free(src_arr); free(dst_arr); free(w_arr);
    free(deg_out); free(cursor);

    fprintf(stderr, "MTX chargé: n=%d m=%d\n", g.n, g.m);
    return g;
}

/*
 * Dispatcher : choisit le parser selon l'extension du fichier.
 */
Graph lire_graphe(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".mtx") == 0)
        return lire_graphe_mtx(filename);
    return lire_graphe_txt(filename);
}

/* =========================================================
 *  Multiplication vecteur x matrice Google (CSR)  (inchangée)
 * ========================================================= */
void mult_vect_mat_csr(const double *pi, const Graph *g, double alpha, double *res) {
    int n = g->n;
    for (int j = 0; j < n; j++) res[j] = 0.0;

    double dangling_mass = 0.0;
    for (int i = 0; i < n; i++) {
        int deg = g->row_ptr[i+1] - g->row_ptr[i];
        if (deg == 0) {
            dangling_mass += alpha * pi[i] / n;
        } else {
            for (int k = g->row_ptr[i]; k < g->row_ptr[i+1]; k++)
                res[g->col_idx[k]] += alpha * pi[i] * g->val[k];
        }
    }

    double uniform = (1.0 - alpha) / n + dangling_mass;
    for (int j = 0; j < n; j++)
        res[j] += uniform;
}

/* =========================================================
 *  Algorithme PageRank  (inchangé)
 * ========================================================= */
double *pagerank(const Graph *g, double alpha, int verbose) {
    int n = g->n;
    double *pi_cur = calloc(n, sizeof(double));
    double *pi_nxt = calloc(n, sizeof(double));
    if (!pi_cur || !pi_nxt) { fprintf(stderr, "Erreur alloc pagerank\n"); exit(1); }

    for (int i = 0; i < n; i++) pi_cur[i] = 1.0 / n;

    mult_vect_mat_csr(pi_cur, g, alpha, pi_nxt);
    double norm = norme_L1(pi_cur, pi_nxt, n);
    int iter = 0;

    while (iter < MAX_IT && norm > EPS) {
        double *tmp = pi_cur; pi_cur = pi_nxt; pi_nxt = tmp;
        mult_vect_mat_csr(pi_cur, g, alpha, pi_nxt);
        norm = norme_L1(pi_cur, pi_nxt, n);
        if (verbose) printf("  Iteration %d - Norme = %.10f\n", iter, norm);
        iter++;
    }
    if (verbose) printf("  Convergence en %d iterations\n\n", iter + 1);

    free(pi_cur);
    return pi_nxt;
}

/* =========================================================
 *  [AJOUT TD2 Q5] Étude précision/itérations
 *  Fait varier ε et mesure le nombre d'itérations à convergence.
 *  Exporte dans un CSV.
 * ========================================================= */
void etude_precision_iterations(const Graph *g, double alpha,
                                 const char *csv_path) {
    /* Valeurs de ε à tester */
    double epsilons[] = { 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7 };
    int    ne         = sizeof(epsilons) / sizeof(epsilons[0]);
    int    n          = g->n;

    FILE *csv = fopen(csv_path, "w");
    if (!csv) { fprintf(stderr, "Impossible de créer %s\n", csv_path); exit(1); }
    fprintf(csv, "epsilon,iterations\n");

    printf("\n=== Étude précision / itérations (alpha=%.2f) ===\n", alpha);
    printf("  %-12s  %s\n", "Epsilon", "Iterations");

    for (int e = 0; e < ne; e++) {
        double eps = epsilons[e];
        double *pi_cur = calloc(n, sizeof(double));
        double *pi_nxt = calloc(n, sizeof(double));
        for (int i = 0; i < n; i++) pi_cur[i] = 1.0 / n;

        mult_vect_mat_csr(pi_cur, g, alpha, pi_nxt);
        double norm = norme_L1(pi_cur, pi_nxt, n);
        int iter = 0;

        while (iter < MAX_IT && norm > eps) {
            double *tmp = pi_cur; pi_cur = pi_nxt; pi_nxt = tmp;
            mult_vect_mat_csr(pi_cur, g, alpha, pi_nxt);
            norm = norme_L1(pi_cur, pi_nxt, n);
            iter++;
        }
        iter++;  /* compte l'itération initiale */

        printf("  %-12.1e  %d\n", eps, iter);
        fprintf(csv, "%.10e,%d\n", eps, iter);

        free(pi_cur);
        free(pi_nxt);
    }
    fclose(csv);
    printf("  → CSV : %s\n", csv_path);
}

/* =========================================================
 *  [AJOUT TD2 Q6] Étude itérations / alpha
 *  Fait varier α sur [0.50, 0.99] et mesure les itérations.
 *  Exporte dans un CSV.
 * ========================================================= */
void etude_iterations_alpha(const Graph *g, const char *csv_path) {
    int    n        = g->n;
    int    n_points = 20;          /* nombre de valeurs d'alpha testées */
    double a_min    = 0.50;
    double a_max    = 0.99;

    FILE *csv = fopen(csv_path, "w");
    if (!csv) { fprintf(stderr, "Impossible de créer %s\n", csv_path); exit(1); }
    fprintf(csv, "alpha,iterations\n");

    printf("\n=== Étude itérations / alpha (eps=%.1e) ===\n", EPS);
    printf("  %-8s  %s\n", "Alpha", "Iterations");

    for (int p = 0; p < n_points; p++) {
        double alpha = a_min + p * (a_max - a_min) / (n_points - 1);

        double *pi_cur = calloc(n, sizeof(double));
        double *pi_nxt = calloc(n, sizeof(double));
        for (int i = 0; i < n; i++) pi_cur[i] = 1.0 / n;

        mult_vect_mat_csr(pi_cur, g, alpha, pi_nxt);
        double norm = norme_L1(pi_cur, pi_nxt, n);
        int iter = 0;

        while (iter < MAX_IT && norm > EPS) {
            double *tmp = pi_cur; pi_cur = pi_nxt; pi_nxt = tmp;
            mult_vect_mat_csr(pi_cur, g, alpha, pi_nxt);
            norm = norme_L1(pi_cur, pi_nxt, n);
            iter++;
        }
        iter++;

        printf("  %-8.4f  %d\n", alpha, iter);
        fprintf(csv, "%.6f,%d\n", alpha, iter);

        free(pi_cur);
        free(pi_nxt);
    }
    fclose(csv);
    printf("  → CSV : %s\n", csv_path);
}

/* =========================================================
 *  [AJOUT TD1 Q1] Vérification dense vs CSR sur petit graphe
 *  Compare les résultats PageRank des deux méthodes.
 *  N'est appelée que si n <= seuil (évite l'explosion mémoire).
 * ========================================================= */
void verifier_dense_vs_csr(const Graph *g, double alpha) {
    int n         = g->n;
    int seuil     = 500;
    if (n > seuil) {
        printf("[Dense] Graphe trop grand (%d > %d), vérification ignorée.\n\n",
               n, seuil);
        return;
    }

    printf("\n=== Vérification matrice dense vs CSR (n=%d) ===\n", n);

    DenseMatrix dm = csr_to_dense_google(g, alpha);
    if (n <= 20) dense_afficher(&dm);

    /* PageRank via matrice dense */
    double *pi_cur = calloc(n, sizeof(double));
    double *pi_nxt = calloc(n, sizeof(double));
    for (int i = 0; i < n; i++) pi_cur[i] = 1.0 / n;

    mult_vect_mat_dense(pi_cur, &dm, pi_nxt);
    double norm = norme_L1(pi_cur, pi_nxt, n);
    int iter = 0;

    while (iter < MAX_IT && norm > EPS) {
        double *tmp = pi_cur; pi_cur = pi_nxt; pi_nxt = tmp;
        mult_vect_mat_dense(pi_cur, &dm, pi_nxt);
        norm = norme_L1(pi_cur, pi_nxt, n);
        iter++;
    }
    double *pr_dense = pi_nxt;
    printf("  Dense  : convergence en %d itérations\n", iter + 1);

    /* PageRank via CSR */
    double *pr_csr = pagerank(g, alpha, 0);

    /* Comparer les deux vecteurs */
    double diff = norme_L1(pr_dense, pr_csr, n);
    printf("  ||PR_dense - PR_CSR||_1 = %.2e  %s\n\n",
           diff, diff < 1e-8 ? "(OK)" : "(ATTENTION: écart non négligeable)");

    free(pi_cur);
    free(pr_csr);
    dense_free(&dm);
}

/* =========================================================
 *  Copie de base commune aux trois attaques  (inchangée)
 * ========================================================= */
static int copier_base(const Graph *g, Graph *g2) {
    int pos = 0;
    g2->row_ptr[0] = 0;
    for (int i = 0; i < g->n; i++) {
        for (int j = g->row_ptr[i]; j < g->row_ptr[i+1]; j++) {
            g2->col_idx[pos] = g->col_idx[j];
            g2->val[pos]     = g->val[j];
            pos++;
        }
        g2->row_ptr[i+1] = pos;
    }
    return pos;
}

/* =========================================================
 *  Attaque 1 : k noeuds isolés pointant vers target  (inchangée)
 * ========================================================= */
Graph attaque_isoles(const Graph *g, int k, int target) {
    Graph g2;
    g2.n = g->n + k;
    g2.m = g->m + k;
    g2.row_ptr = malloc((g2.n + 1) * sizeof(int));
    g2.col_idx = malloc(g2.m * sizeof(int));
    g2.val     = malloc(g2.m * sizeof(double));

    int pos = copier_base(g, &g2);
    for (int a = 0; a < k; a++) {
        g2.col_idx[pos] = target;
        g2.val[pos]     = 1.0;
        pos++;
        g2.row_ptr[g->n + a + 1] = pos;
    }
    return g2;
}

/* =========================================================
 *  Attaque 2 : graphe complet à k noeuds  (inchangée)
 * ========================================================= */
Graph attaque_complet(const Graph *g, int k, int target) {
    if (k > K_MAX_COMPLET) {
        fprintf(stderr, "Erreur: k=%d dépasse K_MAX_COMPLET=%d\n", k, K_MAX_COMPLET);
        exit(1);
    }
    int arcs_new = k * k;
    Graph g2;
    g2.n = g->n + k;
    g2.m = g->m + arcs_new;
    g2.row_ptr = malloc((g2.n + 1) * sizeof(int));
    g2.col_idx = malloc(g2.m * sizeof(int));
    g2.val     = malloc(g2.m * sizeof(double));

    int pos = copier_base(g, &g2);
    for (int a = 0; a < k; a++) {
        double w = 1.0 / k;
        g2.col_idx[pos] = target;
        g2.val[pos]     = w;
        pos++;
        for (int b = 0; b < k; b++) {
            if (b == a) continue;
            g2.col_idx[pos] = g->n + b;
            g2.val[pos]     = w;
            pos++;
        }
        g2.row_ptr[g->n + a + 1] = pos;
    }
    return g2;
}

/* =========================================================
 *  Attaque 3 : anneau à k+1 sommets  (inchangée)
 * ========================================================= */
Graph attaque_anneau(const Graph *g, int k, int target) {
    Graph g2;
    g2.n = g->n + k;
    g2.m = g->m + k + 1;
    g2.row_ptr = malloc((g2.n + 1) * sizeof(int));
    g2.col_idx = malloc(g2.m * sizeof(int));
    g2.val     = malloc(g2.m * sizeof(double));

    int pos = 0;
    g2.row_ptr[0] = 0;
    int n0 = g->n;

    for (int i = 0; i < g->n; i++) {
        int old_deg = g->row_ptr[i+1] - g->row_ptr[i];
        if (i == target) {
            int    new_deg = old_deg + 1;
            double w       = 1.0 / new_deg;
            for (int j = g->row_ptr[i]; j < g->row_ptr[i+1]; j++) {
                g2.col_idx[pos] = g->col_idx[j];
                g2.val[pos]     = w;
                pos++;
            }
            g2.col_idx[pos] = n0;
            g2.val[pos]     = w;
            pos++;
        } else {
            for (int j = g->row_ptr[i]; j < g->row_ptr[i+1]; j++) {
                g2.col_idx[pos] = g->col_idx[j];
                g2.val[pos]     = g->val[j];
                pos++;
            }
        }
        g2.row_ptr[i+1] = pos;
    }

    for (int a = 0; a < k; a++) {
        int dest = (a == k - 1) ? target : (n0 + a + 1);
        g2.col_idx[pos] = dest;
        g2.val[pos]     = 1.0;
        pos++;
        g2.row_ptr[n0 + a + 1] = pos;
    }
    return g2;
}

/* =========================================================
 *  Tri par score décroissant  (inchangé)
 * ========================================================= */
typedef struct { int idx; double score; } Ranked;

int cmp_ranked_desc(const void *a, const void *b) {
    double da = ((const Ranked *)a)->score;
    double db = ((const Ranked *)b)->score;
    return (da < db) - (da > db);
}

/* =========================================================
 *  Choix des trois cibles  (inchangé)
 * ========================================================= */
void choisir_cibles(const double *pr, int n,
                    int *target_fort, int *target_med, int *target_faible) {
    Ranked *r = malloc(n * sizeof(Ranked));
    for (int i = 0; i < n; i++) { r[i].idx = i; r[i].score = pr[i]; }
    qsort(r, n, sizeof(Ranked), cmp_ranked_desc);
    *target_fort   = r[n / 10].idx;
    *target_med    = r[n / 2].idx;
    *target_faible = r[n - 1 - n / 10].idx;
    free(r);
}

/* =========================================================
 *  Affiche top-10, médiane, bottom-10  (inchangé)
 * ========================================================= */
void afficher_classement(const double *pr, int n) {
    Ranked *r = malloc(n * sizeof(Ranked));
    for (int i = 0; i < n; i++) { r[i].idx = i; r[i].score = pr[i]; }
    qsort(r, n, sizeof(Ranked), cmp_ranked_desc);

    printf("--- Top 10 ---\n");
    for (int i = 0; i < 10 && i < n; i++)
        printf("  Page %4d : %.10f\n", r[i].idx, r[i].score);

    printf("--- Page médiane (rang %d) ---\n", n / 2);
    printf("  Page %4d : %.10f\n", r[n/2].idx, r[n/2].score);

    printf("--- Bottom 10 ---\n");
    for (int i = n - 10; i < n; i++)
        printf("  Page %4d : %.10f\n", r[i].idx, r[i].score);

    free(r);
}

/* =========================================================
 *  Boucle principale d'étude  (inchangée)
 * ========================================================= */
void etude_complete(const Graph *g, const double *pr, double alpha,
                    int target_fort, int target_med, int target_faible,
                    int k_max, const char *csv_path) {

    FILE *csv = fopen(csv_path, "w");
    if (!csv) { fprintf(stderr, "Impossible de créer %s\n", csv_path); exit(1); }

    fprintf(csv,
        "k,"
        "fort_isoles,fort_complet,fort_anneau,"
        "med_isoles,med_complet,med_anneau,"
        "faible_isoles,faible_complet,faible_anneau,"
        "fort_gain_isoles,fort_gain_complet,fort_gain_anneau,"
        "med_gain_isoles,med_gain_complet,med_gain_anneau,"
        "faible_gain_isoles,faible_gain_complet,faible_gain_anneau\n");

    int    targets[3] = { target_fort, target_med, target_faible };
    double pr0[3]     = { pr[target_fort], pr[target_med], pr[target_faible] };
    const char *labels[3] = { "Fort  ", "Median", "Faible" };

    printf("\n%5s | %-8s | %10s %10s %10s | %10s %10s %10s\n",
           "k", "Cible", "Isoles", "Complet", "Anneau",
           "Gain_I", "Gain_C", "Gain_A");
    printf("%s\n", "------+----------+-----------------------------------+"
                   "-------------------------------------");

    for (int k = 1; k <= k_max; k++) {
        double scores[3][3];
        for (int c = 0; c < 3; c++) {
            int t = targets[c];
            Graph gi = attaque_isoles (g, k, t);
            Graph gc = attaque_complet(g, k, t);
            Graph ga = attaque_anneau (g, k, t);

            double *pri = pagerank(&gi, alpha, 0);
            double *prc = pagerank(&gc, alpha, 0);
            double *pra = pagerank(&ga, alpha, 0);

            scores[c][0] = pri[t];
            scores[c][1] = prc[t];
            scores[c][2] = pra[t];

            free(pri); free_graph(&gi);
            free(prc); free_graph(&gc);
            free(pra); free_graph(&ga);
        }

        for (int c = 0; c < 3; c++) {
            printf("%5d | %s | %10.7f %10.7f %10.7f | %10.4fx %10.4fx %10.4fx\n",
                   k, labels[c],
                   scores[c][0], scores[c][1], scores[c][2],
                   scores[c][0] / pr0[c],
                   scores[c][1] / pr0[c],
                   scores[c][2] / pr0[c]);
        }
        printf("\n");

        fprintf(csv, "%d,"
                "%.10f,%.10f,%.10f,"
                "%.10f,%.10f,%.10f,"
                "%.10f,%.10f,%.10f,"
                "%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f,"
                "%.6f,%.6f,%.6f\n",
                k,
                scores[0][0], scores[0][1], scores[0][2],
                scores[1][0], scores[1][1], scores[1][2],
                scores[2][0], scores[2][1], scores[2][2],
                scores[0][0]/pr0[0], scores[0][1]/pr0[0], scores[0][2]/pr0[0],
                scores[1][0]/pr0[1], scores[1][1]/pr0[1], scores[1][2]/pr0[1],
                scores[2][0]/pr0[2], scores[2][1]/pr0[2], scores[2][2]/pr0[2]);
    }
    fclose(csv);
    printf("\nRésultats CSV exportés dans : %s\n", csv_path);
}

/* =========================================================
 *  main
 * ========================================================= */
int main(int argc, char *argv[]) {
    const char *filename = "Harvard500.txt";
    double alpha = 0.85;
    int    k_max = 100;

    if (argc >= 2) filename = argv[1];
    if (argc >= 3) alpha    = atof(argv[2]);
    if (argc >= 4) k_max    = atoi(argv[3]);

    if (alpha <= 0.0 || alpha >= 1.0) {
        fprintf(stderr, "Erreur: alpha doit être dans ]0,1[\n");
        return 1;
    }
    if (k_max <= 0) {
        fprintf(stderr, "Erreur: k_max doit être > 0\n");
        return 1;
    }

    printf("=== Google Bombing Simulation ===\n");
    printf("Fichier : %s  |  alpha = %.2f  |  k_max = %d\n\n",
           filename, alpha, k_max);

    Graph g = lire_graphe(filename);
    printf("Graphe chargé : n = %d  m = %d\n\n", g.n, g.m);

    /* -------------------------------------------------------
     *  [AJOUT TD1 Q1] Vérification dense vs CSR
     *  (automatiquement ignorée si n > 500)
     * ----------------------------------------------------- */
    verifier_dense_vs_csr(&g, alpha);

    printf("Calcul du PageRank initial...\n");
    double *pr = pagerank(&g, alpha, 1);

    afficher_classement(pr, g.n);

    int tf, tm, tw;
    choisir_cibles(pr, g.n, &tf, &tm, &tw);

    printf("\n=== Cibles choisies ===\n");
    printf("  Fort   : page %d  (PR = %.10f, rang top 10%%)\n",    tf, pr[tf]);
    printf("  Median : page %d  (PR = %.10f, rang 50%%)\n",        tm, pr[tm]);
    printf("  Faible : page %d  (PR = %.10f, rang bottom 10%%)\n", tw, pr[tw]);

    /* -------------------------------------------------------
     *  [AJOUT TD2 Q5] Courbe précision / itérations
     * ----------------------------------------------------- */
    etude_precision_iterations(&g, alpha, "courbe_precision.csv");

    /* -------------------------------------------------------
     *  [AJOUT TD2 Q6] Courbe itérations / alpha
     * ----------------------------------------------------- */
    etude_iterations_alpha(&g, "courbe_alpha.csv");

    printf("\n=== Étude k = 1..%d ===\n\n", k_max);
    etude_complete(&g, pr, alpha, tf, tm, tw, k_max, "resultats.csv");

    free(pr);
    free_graph(&g);

    return 0;
}