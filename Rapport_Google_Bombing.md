# Projet 7 - Simulation d'un Google Bombing

## Objectif

On part du graphe web `Harvard500.txt` et on calcule d'abord le PageRank initial avec un facteur d'amortissement alpha = 0.85. Ensuite, on ajoute des pages attaquantes qui pointent vers une cible deja presente dans le graphe. Le but est d'observer comment le PageRank de cette cible evolue selon:

- la taille du graphe attaquant;
- le type de graphe attaquant;
- le niveau initial de pertinence de la cible.

Les resultats complets sont dans `bombing_results.csv`.

## Methode

Le programme `test.c` lit le graphe, calcule le PageRank par iteration de puissance, puis choisit trois cibles a partir du classement initial:

| Type de cible | Page | Rang initial | PageRank initial |
|---|---:|---:|---:|
| Forte | 7 | 1 | 0.104366443985 |
| Moyenne | 14 | 251 | 0.000647217386 |
| Faible | 494 | 500 | 0.000300000000 |

Trois structures d'attaque sont testees pour 1, 2, 5, 10, 20, 50, 100 et 200 attaquants:

- `complet`: les attaquants forment un graphe complet dirige sans boucle, et chacun pointe vers la cible;
- `anneau`: la cible appartient a un anneau dirige avec les attaquants;
- `isoles`: chaque attaquant est isole des autres et pointe uniquement vers la cible.

Une attaque est consideree efficace quand le ratio `PageRank apres attaque / PageRank initial` est nettement superieur a 1.

## Meilleurs resultats observes

| Cible | Meilleur type | Attaquants | PageRank apres | Ratio |
|---|---|---:|---:|---:|
| Forte | isoles | 200 | 0.141312768139 | 1.354006 |
| Moyenne | isoles | 200 | 0.036971872453 | 57.124350 |
| Faible | isoles | 200 | 0.036642857143 | 122.142857 |

Les sommets isoles sont donc les plus efficaces dans ces experiences. Ils transmettent presque toute leur probabilite a la cible, alors que les graphes complet et anneau gardent une partie importante de la probabilite dans la structure attaquante.

## Effet de la taille pour les attaquants isoles

| Cible | 1 | 10 | 50 | 100 | 200 |
|---|---:|---:|---:|---:|---:|
| Forte | x1.002 | x1.024 | x1.113 | x1.207 | x1.354 |
| Moyenne | x1.392 | x4.852 | x18.858 | x33.739 | x57.124 |
| Faible | x1.846 | x9.314 | x39.545 | x71.667 | x122.143 |

L'effet augmente fortement avec le nombre d'attaquants, surtout pour les cibles qui avaient un PageRank initial faible ou moyen.

## Comparaison des structures

Avec 200 attaquants:

| Cible | Complet | Anneau | Isoles |
|---|---:|---:|---:|
| Forte | x0.735 | x0.688 | x1.354 |
| Moyenne | x2.543 | x2.593 | x57.124 |
| Faible | x4.650 | x4.762 | x122.143 |

Pour une cible forte, les graphes complet et anneau peuvent meme diminuer le PageRank. Cela arrive parce qu'on ajoute beaucoup de nouvelles pages et beaucoup de liens internes: la masse de probabilite se dilue dans le graphe attaquant au lieu d'etre envoyee directement vers la cible.

Pour une cible faible, le graphe complet et l'anneau ameliorent quand meme le score, mais beaucoup moins que les attaquants isoles. Le meilleur score du graphe complet pour la cible faible est obtenu avec 50 attaquants, avec un ratio d'environ x5.536. Le meilleur score de l'anneau est obtenu avec 20 attaquants, avec un ratio d'environ x6.303.

## Regles empiriques deduites

1. Plus la cible est faible au depart, plus le Google Bombing est spectaculaire en ratio.
2. Les attaquants isoles sont les plus efficaces, car ils concentrent leurs liens sortants vers la cible.
3. Ajouter des liens entre attaquants n'est pas toujours avantageux: cela peut retenir la probabilite dans le sous-graphe attaquant.
4. Pour les graphes complet et anneau, augmenter indefiniment le nombre d'attaquants n'est pas optimal. Dans cette simulation, l'effet atteint un maximum vers 20 a 50 attaquants selon la structure.
5. Une cible deja tres forte est difficile a faire progresser significativement: meme avec 200 attaquants isoles, son PageRank augmente seulement d'environ 35%.

## Conclusion

L'attaque la plus efficace dans ce graphe consiste a creer beaucoup de pages attaquantes isolees qui pointent directement vers la cible. Cette strategie est particulierement puissante contre une cible initialement faible ou moyenne. Les structures plus connectees, comme le graphe complet ou l'anneau, sont moins efficaces car elles repartissent une partie du PageRank entre les attaquants au lieu de le concentrer sur la cible.
