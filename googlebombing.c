/*
 * Google Bombing - Simulation complète (version corrigée)
 * =========================================================
 * Corrections appliquées :
 *   1. fscanf vérifié à chaque lecture
 *   2. Téléportation O(n²) → O(n+m) avec séparation dangling mass
 *   3. Compound literals supprimés : g passé directement
 *   4. Protection mémoire sur k dans attaque_complet
 *
 * Usage : ./googlebombing <fichier.txt> [alpha] [k_max]
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

/* =========================================================
 *  Paramètres globaux
 * ========================================================= */
#define EPS    1e-6
#define MAX_IT 200
#define K_MAX_COMPLET 3000   /* protection mémoire attaque_complet */

/* =========================================================
 *  Structure CSR
 * ========================================================= */
typedef struct {
    int     n;
    int     m;
    int    *row_ptr;
    int    *col_idx;
    double *val;
} Graph;

/* =========================================================
 *  Utilitaires
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
    g->row_ptr = g->col_idx = NULL;
    g->val = NULL;
}

/* =========================================================
 *  Lecture du graphe (format : n tmp m puis m lignes "i j")
 *  CORRECTION : fscanf vérifié à chaque appel
 * ========================================================= */
Graph lire_graphe(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Erreur: impossible d'ouvrir %s\n", filename);
        exit(1);
    }

    Graph g;
    int tmp;
    if (fscanf(f, "%d %d %d", &g.n, &tmp, &g.m) != 3) {
        fprintf(stderr, "Erreur: en-tete du fichier invalide\n");
        fclose(f);
        exit(1);
    }

    g.row_ptr = calloc(g.n + 1, sizeof(int));
    g.col_idx = malloc(g.m * sizeof(int));
    g.val     = malloc(g.m * sizeof(double));

    int *arcs_i = malloc(g.m * sizeof(int));
    int *arcs_j = malloc(g.m * sizeof(int));

    if (!g.row_ptr || !g.col_idx || !g.val || !arcs_i || !arcs_j) {
        fprintf(stderr, "Erreur: allocation memoire echouee\n");
        exit(1);
    }

    for (int k = 0; k < g.m; k++) {
        int i, j;
        if (fscanf(f, "%d %d", &i, &j) != 2) {
            fprintf(stderr, "Erreur: arc %d invalide dans le fichier\n", k);
            fclose(f);
            exit(1);
        }
        arcs_i[k] = i - 1;
        arcs_j[k] = j - 1;
        g.row_ptr[i]++;   /* i encore 1-based → idiome CSR correct */
    }
    fclose(f);

    /* Prefix-sum */
    for (int i = 1; i <= g.n; i++)
        g.row_ptr[i] += g.row_ptr[i - 1];

    int *curseur = calloc(g.n, sizeof(int));
    for (int k = 0; k < g.m; k++) {
        int r   = arcs_i[k];
        int deg = g.row_ptr[r + 1] - g.row_ptr[r];
        int pos = g.row_ptr[r] + curseur[r];
        g.col_idx[pos] = arcs_j[k];
        g.val[pos]     = 1.0 / deg;
        curseur[r]++;
    }

    free(arcs_i);
    free(arcs_j);
    free(curseur);
    return g;
}

/* =========================================================
 *  Multiplication vecteur × matrice Google (CSR)
 *  CORRECTION : téléportation O(n+m) au lieu de O(n²+m)
 *               dangling mass accumulée séparément
 * ========================================================= */
void mult_vect_mat_csr(const double *pi, const Graph *g, double alpha, double *res) {
    int n = g->n;
    for (int j = 0; j < n; j++) res[j] = 0.0;

    double dangling_mass = 0.0;

    for (int i = 0; i < n; i++) {
        int deg = g->row_ptr[i+1] - g->row_ptr[i];
        if (deg == 0) {
            /* nœud dangling : redistribue uniformément via alpha */
            dangling_mass += alpha * pi[i] / n;
        } else {
            for (int k = g->row_ptr[i]; k < g->row_ptr[i+1]; k++)
                res[g->col_idx[k]] += alpha * pi[i] * g->val[k];
        }
    }

    /* Téléportation uniforme + dangling en un seul passage O(n) */
    double uniform = (1.0 - alpha) / n + dangling_mass;
    for (int j = 0; j < n; j++)
        res[j] += uniform;
}

/* =========================================================
 *  Algorithme PageRank (retourne le vecteur alloué)
 * ========================================================= */
double *pagerank(const Graph *g, double alpha, int verbose) {
    int n = g->n;
    double *pi_cur = calloc(n, sizeof(double));
    double *pi_nxt = calloc(n, sizeof(double));

    if (!pi_cur || !pi_nxt) {
        fprintf(stderr, "Erreur: allocation pagerank echouee\n");
        exit(1);
    }

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
 *  Copie de base commune aux trois attaques
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

/* ---------------------------------------------------------
 * Attaque 1 : k nœuds isolés → chacun pointe uniquement vers target
 * CORRECTION : g passé directement (plus de compound literal)
 * --------------------------------------------------------- */
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

/* ---------------------------------------------------------
 * Attaque 2 : clique complet à k nœuds + chacun → target
 * CORRECTION : protection mémoire si k trop grand
 * --------------------------------------------------------- */
Graph attaque_complet(const Graph *g, int k, int target) {
    if (k > K_MAX_COMPLET) {
        fprintf(stderr,
            "Erreur: k=%d depasse K_MAX_COMPLET=%d pour attaque_complet "
            "(risque explosion memoire k^2 arcs)\n", k, K_MAX_COMPLET);
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

/* ---------------------------------------------------------
 * Attaque 3 : anneau à k+1 sommets (la cible fait partie de l'anneau)
 * Structure : n0 → n1 → … → n_{k-1} → target → n0
 * --------------------------------------------------------- */
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
            int new_deg = old_deg + 1;
            double w    = 1.0 / new_deg;
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
 *  Tri par score décroissant
 * ========================================================= */
typedef struct { int idx; double score; } Ranked;

int cmp_ranked_desc(const void *a, const void *b) {
    double da = ((const Ranked *)a)->score;
    double db = ((const Ranked *)b)->score;
    return (da < db) - (da > db);
}

/* =========================================================
 *  Choix des trois cibles : forte / médiane / faible
 *  Utilise top 10%, médiane, bottom 10% pour éviter les cas atypiques
 * ========================================================= */
void choisir_cibles(const double *pr, int n,
                    int *target_fort, int *target_med, int *target_faible) {
    Ranked *r = malloc(n * sizeof(Ranked));
    for (int i = 0; i < n; i++) { r[i].idx = i; r[i].score = pr[i]; }
    qsort(r, n, sizeof(Ranked), cmp_ranked_desc);

    int top10    = n / 10;
    int bottom10 = n - 1 - n / 10;

    *target_fort   = r[top10].idx;
    *target_med    = r[n / 2].idx;
    *target_faible = r[bottom10].idx;

    free(r);
}

/* =========================================================
 *  Affiche top-10, médiane, bottom-10
 * ========================================================= */
void afficher_classement(const double *pr, int n) {
    Ranked *r = malloc(n * sizeof(Ranked));
    for (int i = 0; i < n; i++) { r[i].idx = i; r[i].score = pr[i]; }
    qsort(r, n, sizeof(Ranked), cmp_ranked_desc);

    printf("--- Top 10 ---\n");
    for (int i = 0; i < 10 && i < n; i++)
        printf("  Page %4d : %.10f\n", r[i].idx, r[i].score);

    printf("--- Page mediane (rang %d) ---\n", n / 2);
    printf("  Page %4d : %.10f\n", r[n/2].idx, r[n/2].score);

    printf("--- Bottom 10 ---\n");
    for (int i = n - 10; i < n; i++)
        printf("  Page %4d : %.10f\n", r[i].idx, r[i].score);

    free(r);
}

/* =========================================================
 *  Boucle principale d'étude
 * ========================================================= */
void etude_complete(const Graph *g, const double *pr, double alpha,
                    int target_fort, int target_med, int target_faible,
                    int k_max, const char *csv_path) {

    FILE *csv = fopen(csv_path, "w");
    if (!csv) {
        fprintf(stderr, "Impossible de creer %s\n", csv_path);
        exit(1);
    }

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

            /* CORRECTION : g passé directement, plus de compound literal */
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
    printf("\nResultats CSV exportes dans : %s\n", csv_path);
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
        fprintf(stderr, "Erreur: alpha doit etre dans ]0,1[, recu %.2f\n", alpha);
        return 1;
    }
    if (k_max <= 0) {
        fprintf(stderr, "Erreur: k_max doit etre > 0\n");
        return 1;
    }

    printf("=== Google Bombing Simulation ===\n");
    printf("Fichier : %s  |  alpha = %.2f  |  k_max = %d\n\n",
           filename, alpha, k_max);

    Graph g = lire_graphe(filename);
    printf("Graphe charge : n = %d  m = %d\n\n", g.n, g.m);

    printf("Calcul du PageRank initial...\n");
    double *pr = pagerank(&g, alpha, 1);

    afficher_classement(pr, g.n);

    int tf, tm, tw;
    choisir_cibles(pr, g.n, &tf, &tm, &tw);

    printf("\n=== Cibles choisies ===\n");
    printf("  Fort   : page %d  (PR = %.10f, rang top 10%%)\n",   tf, pr[tf]);
    printf("  Median : page %d  (PR = %.10f, rang 50%%)\n",       tm, pr[tm]);
    printf("  Faible : page %d  (PR = %.10f, rang bottom 10%%)\n", tw, pr[tw]);

    printf("\n=== Etude k = 1..%d ===\n\n", k_max);
    etude_complete(&g, pr, alpha, tf, tm, tw, k_max, "resultats.csv");

    free(pr);
    free_graph(&g);

    return 0;
}
