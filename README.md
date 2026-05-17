# Simulation d'un Google Bombing

Ce depot contient la version finale du projet de methodes de ranking sur la simulation d'un **Google Bombing** avec PageRank.

Le programme principal est [googlebombing_final.c](./googlebombing_final.c). Il lit un graphe Web, calcule le PageRank initial, ajoute virtuellement des structures d'attaque, puis exporte les resultats numeriques.

## Table des matieres

- [Structure du projet](#structure-du-projet)
- [Objectif](#objectif)
- [Compilation](#compilation)
- [Utilisation](#utilisation)
- [Application desktop](#application-desktop)
- [Options principales](#options-principales)
- [Sorties](#sorties)
- [Formats de graphes supportes](#formats-de-graphes-supportes)
- [Remarques](#remarques)

## Structure du projet

```text
.
|-- googlebombing_final.c      # Programme principal final en C
|-- README.md                  # Documentation du projet
|-- rapport.pdf                # Rapport final au format PDF
|-- rapport.tex                # Source LaTeX du rapport final
|-- Harvard500.txt             # Graphe Harvard500 utilise dans les exemples
|-- data/                      # Autres graphes utilises pour les simulations
|-- results/                   # Resultats numeriques produits par le programme
|-- images/                    # Figures utilisees dans le rapport
|-- docs/                      # Documents lies au sujet/projet
|-- demo_google_bombing/       # Application desktop live de demonstration
```

Les fichiers de la version de Dorsaf ne sont pas listes dans cette structure, car ils sont conserves uniquement comme reference et ne font pas partie de la version finale a presenter.

## Objectif

Le projet etudie l'effet d'un Google Bombing sur le classement PageRank d'une page cible.

Le programme permet de :

1. lire un graphe Web ;
2. calculer le PageRank initial ;
3. selectionner trois types de cibles : forte, moyenne et faible ;
4. simuler trois structures d'attaque : sommets isoles, graphe complet et anneau ;
5. mesurer le PageRank apres attaque ;
6. exporter les resultats dans des fichiers CSV.

La version finale utilise une representation creuse de type CSR et une attaque virtuelle. Cela evite de recopier tout le graphe en memoire quand des attaquants sont ajoutes.

## Compilation

Depuis la racine du projet :

```bash
gcc googlebombing_final.c -O2 -Wall -Wextra -o googlebombing_final.exe
```

Sous Windows PowerShell :

```powershell
gcc .\googlebombing_final.c -O2 -Wall -Wextra -o .\googlebombing_final.exe
```

## Utilisation

Commande generale :

```text
googlebombing_final.exe [options] <fichier_graphe> <alpha> <k_max> <sortie.csv>
```

Exemple PowerShell :

```powershell
.\googlebombing_final.exe --prof-k Harvard500.txt 0.85 200 results\resultats_final_harvard.csv
```

Verification rapide de la lecture d'un graphe :

```powershell
.\googlebombing_final.exe Harvard500.txt 0.85 0 results\check.csv
```

Simulation plus rapide sur un grand graphe :

```powershell
.\googlebombing_final.exe --k-step 5 data\wikipedia-20051105V2.txt 0.85 20 results\resultats_wikipedia_quick.csv
```

Balayage de plusieurs valeurs de `alpha` :

```powershell
.\googlebombing_final.exe --alpha-sweep "0.50,0.70,0.85,0.95" --k-step 2 Harvard500.txt 0.85 50 results\resultats_alpha.csv
```

## Application desktop

Une application de demonstration live est disponible dans `demo_google_bombing/`.

Pour la lancer :

```powershell
python demo_google_bombing\application_demo.py
```

Elle permet de modifier les parametres principaux en direct et de visualiser l'impact de l'attaque sur le PageRank de la cible.

## Options principales

| Option | Role |
| --- | --- |
| `--prof-k` | Utilise les valeurs de `k` demandees pour un tableau compact. |
| `--k-step n` | Teste les valeurs de `k` avec un pas de `n`, utile pour les grands graphes. |
| `--alpha-sweep "a1,a2,..."` | Lance plusieurs simulations avec plusieurs valeurs de `alpha`. |
| `--quiet` | Reduit l'affichage dans le terminal. |

Arguments :

| Argument | Role |
| --- | --- |
| `<fichier_graphe>` | Graphe d'entree. |
| `<alpha>` | Facteur d'amortissement PageRank dans `]0,1[`. |
| `<k_max>` | Nombre maximum d'attaquants. |
| `<sortie.csv>` | Fichier CSV a produire. |

## Sorties

Les resultats produits contiennent notamment :

| Colonne | Description |
| --- | --- |
| `fichier` | Graphe utilise. |
| `n` | Nombre de sommets. |
| `m` | Nombre d'arcs. |
| `alpha` | Facteur d'amortissement PageRank. |
| `k` | Nombre d'attaquants. |
| `type_cible` | Cible forte, moyenne ou faible. |
| `page_cible` | Identifiant de la page cible. |
| `rank_initial` | Rang initial de la cible. |
| `pagerank_initial` | PageRank avant attaque. |
| `type_attaque` | Structure d'attaque utilisee. |
| `pagerank_apres` | PageRank apres attaque. |
| `augmentation` | Difference entre apres et avant. |
| `ratio` | Gain relatif. |
| `iterations` | Nombre d'iterations de convergence. |

## Formats de graphes supportes

Le programme supporte :

- le format Harvard ;
- le format matrice creuse ponderee ;
- le format Matrix Market `.mtx`.

Les indices des graphes sont supposes etre en base 1.

## Remarques

- Pour le rendu final, utiliser `googlebombing_final.c`, `Harvard500.txt`, `rapport.pdf`, `data/`, `results/`, `images/` et `demo_google_bombing/`.
- Les fichiers de Dorsaf sont conserves uniquement comme reference et ne doivent pas etre modifies.
- Pour les grands graphes, utiliser `--k-step` afin de reduire le temps d'execution.
- Aucun script `build.ps1` n'est necessaire : la compilation se fait directement avec `gcc`.
