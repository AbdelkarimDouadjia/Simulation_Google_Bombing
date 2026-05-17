"""
Application desktop autonome pour simuler un Google Bombing.

Lancez depuis la racine du projet :
    python demo_google_bombing/application_demo.py

Ce fichier contient a la fois le modele PageRank, la simulation d'attaque,
l'interface Tkinter et l'export CSV. Il ne modifie pas le code C original.
"""

from __future__ import annotations

from dataclasses import dataclass
import csv
import math
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Iterable

RACINE_PROJET = Path(__file__).resolve().parents[1]
DOSSIER_DEMO = Path(__file__).resolve().parent
HARVARD_PAR_DEFAUT = RACINE_PROJET / "data" / "Harvard500.txt"

EPSILON = 1e-10
MAX_ITERATIONS = 1_000

ATTAQUES = ("isoles", "complet", "anneau")
CIBLES = ("forte", "moyenne", "faible")


@dataclass(frozen=True)
class GrapheWeb:
    """Graphe oriente stocke en listes d'adjacence ponderees."""

    nom: str
    sorties: tuple[tuple[tuple[int, float], ...], ...]

    @property
    def n(self) -> int:
        return len(self.sorties)

    @property
    def m(self) -> int:
        return sum(len(ligne) for ligne in self.sorties)


@dataclass(frozen=True)
class Cible:
    type_cible: str
    page: int
    rang: int
    score_initial: float


@dataclass(frozen=True)
class ResultatSimulation:
    graphe: str
    n: int
    m: int
    alpha: float
    k: int
    type_cible: str
    type_attaque: str
    page_cible: int
    rang_initial: int
    pagerank_initial: float
    pagerank_apres: float
    augmentation: float
    ratio: float
    iterations_base: int
    iterations_attaque: int


def resultat_en_ligne(resultat: ResultatSimulation) -> dict[str, int | float | str]:
    return {
        "graphe": resultat.graphe,
        "n": resultat.n,
        "m": resultat.m,
        "alpha": resultat.alpha,
        "k": resultat.k,
        "type_cible": resultat.type_cible,
        "page_cible": resultat.page_cible,
        "rank_initial": resultat.rang_initial,
        "pagerank_initial": resultat.pagerank_initial,
        "type_attaque": resultat.type_attaque,
        "pagerank_apres": resultat.pagerank_apres,
        "augmentation": resultat.augmentation,
        "ratio": resultat.ratio,
        "iterations_base": resultat.iterations_base,
        "iterations_attaque": resultat.iterations_attaque,
    }


def normaliser_attaque(type_attaque: str) -> str:
    valeur = type_attaque.lower().strip()
    aliases = {
        "isolés": "isoles",
        "isolé": "isoles",
        "isole": "isoles",
        "isoles": "isoles",
        "graphe complet": "complet",
        "complet": "complet",
        "anneau": "anneau",
        "cycle": "anneau",
    }
    if valeur not in aliases:
        raise ValueError(f"Type d'attaque inconnu : {type_attaque}")
    return aliases[valeur]


def etiquette_attaque(type_attaque: str) -> str:
    noms = {
        "isoles": "Sommets isolés",
        "complet": "Graphe complet",
        "anneau": "Anneau",
    }
    return noms[normaliser_attaque(type_attaque)]


def etiquette_cible(type_cible: str) -> str:
    noms = {
        "forte": "Cible forte",
        "moyenne": "Cible moyenne",
        "faible": "Cible faible",
    }
    if type_cible not in noms:
        raise ValueError(f"Type de cible inconnu : {type_cible}")
    return noms[type_cible]


def _avec_poids_uniformes(nom: str, arcs: Iterable[tuple[int, int]], n: int) -> GrapheWeb:
    lignes: list[list[int]] = [[] for _ in range(n)]
    for source, destination in arcs:
        if 0 <= source < n and 0 <= destination < n:
            lignes[source].append(destination)

    sorties: list[tuple[tuple[int, float], ...]] = []
    for ligne in lignes:
        if not ligne:
            sorties.append(())
            continue
        poids = 1.0 / len(ligne)
        sorties.append(tuple((destination, poids) for destination in ligne))
    return GrapheWeb(nom=nom, sorties=tuple(sorties))


def creer_graphe_demo() -> GrapheWeb:
    """Petit graphe visuel utilise par defaut dans l'application."""

    arcs = [
        (0, 1), (0, 2), (0, 3),
        (1, 2), (1, 4),
        (2, 0), (2, 4), (2, 5),
        (3, 2), (3, 6),
        (4, 2), (4, 7), (4, 8),
        (5, 4),
        (6, 3), (6, 4), (6, 9),
        (7, 4), (7, 8),
        (8, 4),
        (9, 8), (9, 10), (9, 11),
        (10, 9),
        (11, 4), (11, 10),
    ]
    return _avec_poids_uniformes("Graphe pédagogique", arcs, 12)


def lire_graphe(chemin: str | Path) -> GrapheWeb:
    """
    Lit les deux formats presents dans le projet :
    - Harvard500 : premiere ligne `n n m`, puis arcs `i j` ;
    - matrices creuses : `n m`, puis lignes `i deg j1 w1 ...`.
    """

    path = Path(chemin)
    lignes = [ligne.strip() for ligne in path.read_text(encoding="utf-8").splitlines() if ligne.strip()]
    if not lignes:
        raise ValueError(f"Fichier vide : {path}")

    premiere = lignes[0].split()
    if len(premiere) >= 3:
        n = int(premiere[0])
        arcs: list[tuple[int, int]] = []
        for ligne in lignes[1:]:
            morceaux = ligne.split()
            if len(morceaux) >= 2:
                arcs.append((int(morceaux[0]) - 1, int(morceaux[1]) - 1))
        return _avec_poids_uniformes(path.name, arcs, n)

    if len(premiere) == 2:
        n = int(premiere[0])
        lignes_donnees = lignes[1:]
    else:
        n = int(premiere[0])
        lignes_donnees = lignes[2:]

    sorties: list[list[tuple[int, float]]] = [[] for _ in range(n)]
    for ligne in lignes_donnees:
        morceaux = ligne.split()
        if len(morceaux) < 2:
            continue
        source = int(morceaux[0]) - 1
        degre = int(morceaux[1])
        if not 0 <= source < n or degre <= 0:
            continue

        paires = morceaux[2:]
        destinations: list[int] = []
        poids: list[float] = []
        for i in range(0, min(len(paires), 2 * degre), 2):
            destinations.append(int(paires[i]) - 1)
            poids.append(float(paires[i + 1]) if i + 1 < len(paires) else 0.0)

        somme = sum(poids)
        if somme <= 0.0:
            poids = [1.0 / len(destinations)] * len(destinations)
        else:
            poids = [p / somme for p in poids]

        for destination, valeur in zip(destinations, poids):
            if 0 <= destination < n:
                sorties[source].append((destination, valeur))

    return GrapheWeb(
        nom=path.name,
        sorties=tuple(tuple(ligne) for ligne in sorties),
    )


def _distance_l1(a: list[float], b: list[float]) -> float:
    return sum(abs(x - y) for x, y in zip(a, b))


def pagerank_base(
    graphe: GrapheWeb,
    alpha: float = 0.85,
    epsilon: float = EPSILON,
    max_iterations: int = MAX_ITERATIONS,
) -> tuple[list[float], int]:
    n = graphe.n
    courant = [1.0 / n] * n
    suivant = [0.0] * n

    for iteration in range(1, max_iterations + 1):
        for i in range(n):
            suivant[i] = 0.0

        masse_dangling = 0.0
        for source, voisins in enumerate(graphe.sorties):
            if not voisins:
                masse_dangling += courant[source]
                continue
            for destination, poids in voisins:
                suivant[destination] += alpha * courant[source] * poids

        uniforme = (1.0 - alpha) / n + alpha * masse_dangling / n
        for i in range(n):
            suivant[i] += uniforme

        diff = _distance_l1(courant, suivant)
        courant, suivant = suivant, courant
        if diff <= epsilon:
            return courant[:], iteration

    return courant[:], max_iterations


def classement(scores: list[float]) -> list[tuple[int, float]]:
    return sorted(enumerate(scores), key=lambda item: (-item[1], item[0]))


def selectionner_cibles(scores: list[float]) -> dict[str, Cible]:
    ordre = classement(scores)
    positions = {
        "forte": 0,
        "moyenne": len(ordre) // 2,
        "faible": len(ordre) - 1,
    }
    cibles: dict[str, Cible] = {}
    for type_cible, position in positions.items():
        page, score = ordre[position]
        cibles[type_cible] = Cible(
            type_cible=type_cible,
            page=page,
            rang=position + 1,
            score_initial=score,
        )
    return cibles


def _initialisation_chaude(scores_base: list[float], k: int) -> list[float]:
    n = len(scores_base)
    total = n + k
    facteur = n / total
    courant = [score * facteur for score in scores_base]
    courant.extend([1.0 / total] * k)
    return courant


def _multiplier_attaque(
    graphe: GrapheWeb,
    courant: list[float],
    alpha: float,
    type_attaque: str,
    cible: int,
    k: int,
) -> list[float]:
    type_attaque = normaliser_attaque(type_attaque)
    n = graphe.n
    total = n + k
    premier_attaquant = n
    suivant = [0.0] * total
    masse_dangling = 0.0

    for source, voisins in enumerate(graphe.sorties):
        degre = len(voisins)
        if type_attaque == "anneau" and source == cible:
            if degre == 0:
                suivant[premier_attaquant] += alpha * courant[source]
            else:
                facteur_ancien = degre / (degre + 1)
                for destination, poids in voisins:
                    suivant[destination] += alpha * courant[source] * poids * facteur_ancien
                suivant[premier_attaquant] += alpha * courant[source] / (degre + 1)
        elif degre == 0:
            masse_dangling += courant[source]
        else:
            for destination, poids in voisins:
                suivant[destination] += alpha * courant[source] * poids

    masse_attaquants = sum(courant[premier_attaquant:])
    if type_attaque == "isoles":
        suivant[cible] += alpha * masse_attaquants
    elif type_attaque == "complet":
        poids = 1.0 / k
        suivant[cible] += alpha * masse_attaquants * poids
        for a in range(k):
            index = premier_attaquant + a
            suivant[index] += alpha * (masse_attaquants - courant[index]) * poids
    elif type_attaque == "anneau":
        for a in range(k):
            source = premier_attaquant + a
            destination = cible if a == k - 1 else source + 1
            suivant[destination] += alpha * courant[source]

    uniforme = (1.0 - alpha) / total + alpha * masse_dangling / total
    for i in range(total):
        suivant[i] += uniforme
    return suivant


def pagerank_attaque(
    graphe: GrapheWeb,
    scores_base: list[float],
    cible: int,
    k: int,
    type_attaque: str,
    alpha: float = 0.85,
    epsilon: float = EPSILON,
    max_iterations: int = MAX_ITERATIONS,
) -> tuple[float, int]:
    if k <= 0:
        raise ValueError("Le nombre d'attaquants doit etre strictement positif.")

    courant = _initialisation_chaude(scores_base, k)
    for iteration in range(1, max_iterations + 1):
        suivant = _multiplier_attaque(graphe, courant, alpha, type_attaque, cible, k)
        diff = _distance_l1(courant, suivant)
        courant = suivant
        if diff <= epsilon:
            return courant[cible], iteration

    return courant[cible], max_iterations


def simuler(
    graphe: GrapheWeb,
    alpha: float,
    k: int,
    type_cible: str,
    type_attaque: str,
) -> ResultatSimulation:
    if not 0.0 < alpha < 1.0:
        raise ValueError("Alpha doit etre dans l'intervalle ]0, 1[.")
    if type_cible not in CIBLES:
        raise ValueError(f"Type de cible inconnu : {type_cible}")

    type_attaque = normaliser_attaque(type_attaque)
    scores_base, iterations_base = pagerank_base(graphe, alpha)
    cibles = selectionner_cibles(scores_base)
    cible = cibles[type_cible]
    score_apres, iterations_attaque = pagerank_attaque(
        graphe=graphe,
        scores_base=scores_base,
        cible=cible.page,
        k=k,
        type_attaque=type_attaque,
        alpha=alpha,
    )
    augmentation = score_apres - cible.score_initial
    ratio = score_apres / cible.score_initial if cible.score_initial > 0.0 else 0.0

    return ResultatSimulation(
        graphe=graphe.nom,
        n=graphe.n,
        m=graphe.m,
        alpha=alpha,
        k=k,
        type_cible=type_cible,
        type_attaque=type_attaque,
        page_cible=cible.page + 1,
        rang_initial=cible.rang,
        pagerank_initial=cible.score_initial,
        pagerank_apres=score_apres,
        augmentation=augmentation,
        ratio=ratio,
        iterations_base=iterations_base,
        iterations_attaque=iterations_attaque,
    )


def simuler_resultats_complets(
    graphe: GrapheWeb,
    alpha: float = 0.85,
    k_max: int = 50,
    pas: int = 1,
) -> list[ResultatSimulation]:
    """Calcule toutes les combinaisons demandees : cibles x attaques x k."""

    if k_max <= 0:
        raise ValueError("k_max doit etre strictement positif.")
    if pas <= 0:
        raise ValueError("Le pas doit etre strictement positif.")

    scores_base, iterations_base = pagerank_base(graphe, alpha)
    cibles = selectionner_cibles(scores_base)
    resultats: list[ResultatSimulation] = []

    valeurs_k = list(range(1, k_max + 1, pas))
    if valeurs_k[-1] != k_max:
        valeurs_k.append(k_max)

    for k in valeurs_k:
        for type_cible in CIBLES:
            cible = cibles[type_cible]
            for type_attaque in ATTAQUES:
                score_apres, iterations_attaque = pagerank_attaque(
                    graphe=graphe,
                    scores_base=scores_base,
                    cible=cible.page,
                    k=k,
                    type_attaque=type_attaque,
                    alpha=alpha,
                )
                augmentation = score_apres - cible.score_initial
                ratio = score_apres / cible.score_initial if cible.score_initial > 0.0 else 0.0
                resultats.append(
                    ResultatSimulation(
                        graphe=graphe.nom,
                        n=graphe.n,
                        m=graphe.m,
                        alpha=alpha,
                        k=k,
                        type_cible=type_cible,
                        type_attaque=type_attaque,
                        page_cible=cible.page + 1,
                        rang_initial=cible.rang,
                        pagerank_initial=cible.score_initial,
                        pagerank_apres=score_apres,
                        augmentation=augmentation,
                        ratio=ratio,
                        iterations_base=iterations_base,
                        iterations_attaque=iterations_attaque,
                    )
                )

    return resultats


def exporter_csv(resultats: list[ResultatSimulation], chemin: str | Path) -> Path:
    path = Path(chemin)
    path.parent.mkdir(parents=True, exist_ok=True)
    champs = [
        "graphe",
        "n",
        "m",
        "alpha",
        "k",
        "type_cible",
        "page_cible",
        "rank_initial",
        "pagerank_initial",
        "type_attaque",
        "pagerank_apres",
        "augmentation",
        "ratio",
        "iterations_base",
        "iterations_attaque",
    ]
    with path.open("w", encoding="utf-8", newline="") as fichier:
        writer = csv.DictWriter(fichier, fieldnames=champs)
        writer.writeheader()
        for resultat in resultats:
            writer.writerow(resultat_en_ligne(resultat))
    return path


def k_demandes(k_max: int) -> list[int]:
    """Valeurs proches du code pedagogique `src/test.c`."""

    base = [1, 2, 5, 10, 20, 50, 100, 200]
    valeurs = [k for k in base if k <= k_max]
    if k_max not in valeurs:
        valeurs.append(k_max)
    return sorted(set(valeurs))


def exporter_csv_type_test(resultats, dossier: Path) -> Path:
    """CSV dans l'esprit de `src/test.c`."""

    sortie = dossier / "resultats_professeur_google_bombing.csv"
    champs = [
        "type_cible",
        "id_cible",
        "rank_initial",
        "pagerank_initial",
        "type_attaque",
        "nb_attaquants",
        "pagerank_apres",
        "augmentation",
        "ratio",
        "iterations",
    ]
    with sortie.open("w", encoding="utf-8", newline="") as fichier:
        writer = csv.DictWriter(fichier, fieldnames=champs)
        writer.writeheader()
        for r in resultats:
            writer.writerow(
                {
                    "type_cible": r.type_cible,
                    "id_cible": r.page_cible,
                    "rank_initial": r.rang_initial,
                    "pagerank_initial": f"{r.pagerank_initial:.15f}",
                    "type_attaque": r.type_attaque,
                    "nb_attaquants": r.k,
                    "pagerank_apres": f"{r.pagerank_apres:.15f}",
                    "augmentation": f"{r.augmentation:.15f}",
                    "ratio": f"{r.ratio:.6f}",
                    "iterations": r.iterations_attaque,
                }
            )
    return sortie


def ecrire_synthese(resultats, dossier: Path) -> Path:
    sortie = dossier / "synthese_regles_empiriques.md"
    meilleurs = {}
    for r in resultats:
        cle = (r.type_cible, r.type_attaque)
        if cle not in meilleurs or r.ratio > meilleurs[cle].ratio:
            meilleurs[cle] = r

    lignes = [
        "# Synthèse - simulation Google Bombing",
        "",
        "## Meilleurs ratios observés",
        "",
        "| Cible | Attaque | k optimal | Ratio | PageRank après |",
        "|---|---|---:|---:|---:|",
    ]
    for cible in CIBLES:
        for attaque in ATTAQUES:
            r = meilleurs[(cible, attaque)]
            lignes.append(f"| {cible} | {attaque} | {r.k} | {r.ratio:.3f} | {r.pagerank_apres:.8f} |")

    lignes.extend(
        [
            "",
            "## Règles empiriques",
            "",
            "- Les sommets isolés sont souvent les plus efficaces quand k augmente, car toute la masse pointe directement vers la cible.",
            "- L'anneau peut être très compétitif pour des valeurs de k petites ou moyennes, car il recycle la masse par la cible.",
            "- Le graphe complet disperse une partie de la masse entre attaquants ; il peut donc avoir un optimum interne.",
            "- Une cible initialement faible obtient en général le meilleur ratio, car son PageRank de départ est bas.",
        ]
    )
    sortie.write_text("\n".join(lignes), encoding="utf-8")
    return sortie


def generer_exports(
    graphe: GrapheWeb | None = None,
    alpha: float = 0.85,
    k_max: int = 200,
    pas: int | None = None,
    dossier: str | Path = DOSSIER_DEMO / "exports",
) -> dict[str, Path]:
    dossier = Path(dossier)
    dossier.mkdir(parents=True, exist_ok=True)
    if graphe is None:
        graphe = lire_graphe(HARVARD_PAR_DEFAUT) if HARVARD_PAR_DEFAUT.exists() else creer_graphe_demo()

    valeurs = k_demandes(k_max) if pas is None else list(range(1, k_max + 1, max(1, pas)))
    if valeurs[-1] != k_max:
        valeurs.append(k_max)

    scores_base, iterations_base = pagerank_base(graphe, alpha)
    cibles = selectionner_cibles(scores_base)
    resultats: list[ResultatSimulation] = []
    for k in valeurs:
        for type_cible in CIBLES:
            cible = cibles[type_cible]
            for type_attaque in ATTAQUES:
                score_apres, iterations_attaque = pagerank_attaque(
                    graphe,
                    scores_base,
                    cible.page,
                    k,
                    type_attaque,
                    alpha,
                )
                augmentation = score_apres - cible.score_initial
                ratio = score_apres / cible.score_initial if cible.score_initial > 0.0 else 0.0
                resultats.append(
                    ResultatSimulation(
                        graphe=graphe.nom,
                        n=graphe.n,
                        m=graphe.m,
                        alpha=alpha,
                        k=k,
                        type_cible=type_cible,
                        type_attaque=type_attaque,
                        page_cible=cible.page + 1,
                        rang_initial=cible.rang,
                        pagerank_initial=cible.score_initial,
                        pagerank_apres=score_apres,
                        augmentation=augmentation,
                        ratio=ratio,
                        iterations_base=iterations_base,
                        iterations_attaque=iterations_attaque,
                    )
                )

    csv_path = exporter_csv(resultats, dossier / "resultats_complets_google_bombing.csv")
    csv_prof_path = exporter_csv_type_test(resultats, dossier)
    synthese_path = ecrire_synthese(resultats, dossier)
    return {
        "csv_complet": csv_path,
        "csv_professeur": csv_prof_path,
        "synthese": synthese_path,
    }


COL = {
    "bg": "#edf4fa",
    "card": "#ffffff",
    "panel": "#f7fbff",
    "line": "#d3e0ec",
    "line_soft": "#e7eef6",
    "text": "#142033",
    "muted": "#7e8b9a",
    "navy": "#1f5f96",
    "navy_dark": "#17466f",
    "blue": "#2778b8",
    "blue_soft": "#e7f1fb",
    "orange": "#f0a43a",
    "orange_soft": "#fff2d8",
    "red": "#d9544d",
    "red_soft": "#ffe2d9",
    "green": "#2f8a61",
    "green_soft": "#dff3e8",
    "dark": "#253443",
}


class GoogleBombingApp(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Simulation Google Bombing")
        self.geometry("1320x740")
        self.minsize(1120, 640)
        self.configure(bg=COL["bg"])

        self.graphe = creer_graphe_demo()
        self.chemin_fichier = HARVARD_PAR_DEFAUT
        self.resultat: ResultatSimulation | None = None
        self.scores_base: list[float] = []
        self.cibles = {}
        self.cache_base: dict[tuple[int, float], tuple[list[float], dict, int]] = {}
        self.cache_apercu: dict[tuple[int, float, int], list[tuple]] = {}

        self.modele = tk.StringVar(value="Pédagogique")
        self.alpha = tk.DoubleVar(value=0.85)
        self.k = tk.IntVar(value=8)
        self.k_max = tk.IntVar(value=200)
        self.type_cible = tk.StringVar(value="faible")
        self.type_attaque = tk.StringVar(value="isoles")
        self.phase = tk.StringVar(value="resultats")

        self.animation = True
        self.tick = 0
        self._requete = 0
        self._after_recalcul: str | None = None
        self._calcul_en_cours = False
        self._recalcul_en_attente = False
        self._export_en_cours = False
        self._option_groups: list[tuple[tk.StringVar, list[tk.Button]]] = []
        self._phase_buttons: dict[str, tk.Button] = {}

        self._style()
        self._layout()
        self._programmer_recalcul(0)
        self._boucle_animation()

    def _style(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("Treeview", rowheight=24, font=("Segoe UI", 8), background="#ffffff", fieldbackground="#ffffff")
        style.configure("Treeview.Heading", font=("Segoe UI", 8, "bold"), background=COL["panel"], foreground=COL["text"])

    def _layout(self) -> None:
        root = tk.Frame(self, bg=COL["bg"], padx=10, pady=10)
        root.pack(fill=tk.BOTH, expand=True)
        root.grid_columnconfigure(0, minsize=310, weight=0)
        root.grid_columnconfigure(1, weight=1)
        root.grid_columnconfigure(2, minsize=430, weight=0)
        root.grid_rowconfigure(0, weight=1)

        left = self._card(root, width=310)
        center = self._card(root)
        right = self._card(root, width=430)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        center.grid(row=0, column=1, sticky="nsew")
        right.grid(row=0, column=2, sticky="nsew", padx=(10, 0))

        left.grid_columnconfigure(0, weight=1)
        center.grid_columnconfigure(0, weight=1)
        center.grid_rowconfigure(2, weight=1)
        right.grid_columnconfigure(0, weight=1)
        right.grid_rowconfigure(3, weight=0)
        right.grid_rowconfigure(5, weight=1)

        self._left(left)
        self._center(center)
        self._right(right)

    def _card(self, parent: tk.Widget, width: int | None = None) -> tk.Frame:
        frame = tk.Frame(parent, bg=COL["card"], highlightthickness=1, highlightbackground=COL["line"], padx=12, pady=12)
        if width is not None:
            frame.configure(width=width)
            frame.grid_propagate(False)
        return frame

    def _left(self, parent: tk.Frame) -> None:
        tk.Label(parent, text="Simulation Google Bombing", bg=COL["card"], fg=COL["text"], font=("Segoe UI", 15, "bold")).grid(row=0, column=0, sticky="w")
        tk.Label(parent, text="Paramètres live pour piloter l'attaque PageRank.", bg=COL["card"], fg=COL["muted"], font=("Segoe UI", 9), wraplength=250, justify=tk.LEFT).grid(row=1, column=0, sticky="ew", pady=(3, 10))

        row = 2
        row = self._segmented(parent, row, "Modèle", self.modele, ("Pédagogique", "Harvard500", "Fichier"), self._changer_modele)
        self._button(parent, "📂  Choisir un graphe", self._choisir_fichier, "ghost").grid(row=row, column=0, sticky="ew", pady=(0, 8))
        row += 1
        row = self._slider(parent, row, "alpha PageRank", self.alpha, 0.50, 0.95, "{:.2f}")
        row = self._slider(parent, row, "k attaquants", self.k, 1, 80, "{:.0f}", integer=True)
        row = self._slider(parent, row, "k max export", self.k_max, 10, 200, "{:.0f}", integer=True)
        row = self._segmented(parent, row, "Type de cible", self.type_cible, CIBLES, self._programmer_recalcul)
        row = self._segmented(parent, row, "Structure", self.type_attaque, ATTAQUES, self._programmer_recalcul)

        actions = tk.Frame(parent, bg=COL["card"])
        actions.grid(row=row, column=0, sticky="ew", pady=(10, 8))
        actions.grid_columnconfigure(0, weight=1)
        actions.grid_columnconfigure(1, weight=1)
        self.pause_button = self._button(actions, "⏸  Pause", self._pause, "ghost")
        self.pause_button.grid(row=0, column=0, sticky="ew", padx=(0, 5))
        self._button(actions, "⟳  Recalculer", lambda: self._programmer_recalcul(0), "ghost").grid(row=0, column=1, sticky="ew", padx=(5, 0))
        row += 1
        self._button(parent, "⬇  Exporter CSV demandé", self._exporter, "primary").grid(row=row, column=0, sticky="ew")
        row += 1
        self._legend(parent, row)

    def _center(self, parent: tk.Frame) -> None:
        top = tk.Frame(parent, bg=COL["card"])
        top.grid(row=0, column=0, sticky="ew")
        top.grid_columnconfigure(0, weight=1)
        tk.Label(top, text="Google Bombing : simulation PageRank", bg=COL["card"], fg=COL["text"], font=("Segoe UI", 17, "bold")).grid(row=0, column=0, sticky="w")
        tk.Label(top, text="Cible déjà présente, attaquants ajoutés, puis mesure du gain.", bg=COL["card"], fg=COL["muted"], font=("Segoe UI", 9)).grid(row=1, column=0, sticky="w", pady=(2, 0))
        self.status_chip = tk.Label(top, text="calcul", bg=COL["orange_soft"], fg=COL["orange"], font=("Segoe UI", 9, "bold"), padx=10, pady=4)
        self.status_chip.grid(row=0, column=1, rowspan=2, sticky="e")

        self.phase_bar = tk.Frame(parent, bg=COL["panel"], highlightthickness=1, highlightbackground=COL["line"], padx=6, pady=6)
        self.phase_bar.grid(row=1, column=0, sticky="ew", pady=(10, 8))
        for i, (phase, label) in enumerate((("initial", "1  PageRank"), ("attaquants", "2  Attaque"), ("impact", "3  Impact"), ("resultats", "4  Résultats"))):
            self.phase_bar.grid_columnconfigure(i, weight=1)
            btn = tk.Button(self.phase_bar, text=label, command=lambda p=phase: self._set_phase(p), relief=tk.FLAT, bd=0, font=("Segoe UI", 9, "bold"), padx=10, pady=8, cursor="hand2")
            btn.grid(row=0, column=i, sticky="ew", padx=3)
            self._phase_buttons[phase] = btn

        self.canvas = tk.Canvas(parent, bg=COL["card"], highlightthickness=1, highlightbackground=COL["line"])
        self.canvas.grid(row=2, column=0, sticky="nsew")
        self.canvas.bind("<Configure>", lambda _event: self._dessiner())
        self._refresh_phase_buttons()

    def _right(self, parent: tk.Frame) -> None:
        tk.Label(parent, text="Métriques live", bg=COL["card"], fg=COL["text"], font=("Segoe UI", 12, "bold")).grid(row=0, column=0, sticky="w")
        metrics = tk.Frame(parent, bg=COL["card"])
        metrics.grid(row=1, column=0, sticky="ew", pady=(8, 10))
        metrics.grid_columnconfigure(0, weight=1)
        metrics.grid_columnconfigure(1, weight=1)
        self.metrics: dict[str, tk.Label] = {}
        for i, (key, label) in enumerate((("alpha", "alpha"), ("k", "attaquants"), ("avant", "PR avant"), ("apres", "PR après"), ("ratio", "ratio"), ("gain", "gain"))):
            tile = tk.Frame(metrics, bg=COL["panel"], highlightthickness=1, highlightbackground=COL["line"], padx=8, pady=6)
            tile.grid(row=i // 2, column=i % 2, sticky="ew", padx=4, pady=4)
            tk.Label(tile, text=label, bg=COL["panel"], fg=COL["muted"], font=("Segoe UI", 8)).pack(anchor=tk.W)
            value = tk.Label(tile, text="-", bg=COL["panel"], fg=COL["text"], font=("Consolas", 14, "bold"))
            value.pack(anchor=tk.W)
            self.metrics[key] = value

        tk.Label(parent, text="Événement courant", bg=COL["card"], fg=COL["text"], font=("Segoe UI", 12, "bold")).grid(row=2, column=0, sticky="w", pady=(4, 4))
        event_box = tk.Frame(parent, bg=COL["panel"], highlightthickness=1, highlightbackground=COL["line"], padx=8, pady=8)
        event_box.grid(row=3, column=0, sticky="nsew")
        event_box.grid_columnconfigure(0, weight=1)
        event_box.grid_rowconfigure(0, weight=1)
        self.event = tk.Text(event_box, bg=COL["panel"], fg=COL["muted"], relief=tk.FLAT, wrap=tk.WORD, font=("Segoe UI", 9), height=7, padx=4, pady=4)
        self.event.grid(row=0, column=0, sticky="nsew")

        self.results_title = tk.Label(parent, text="Résultats demandés", bg=COL["card"], fg=COL["text"], font=("Segoe UI", 12, "bold"))
        self.results_title.grid(row=4, column=0, sticky="w", pady=(10, 4))
        table_frame = tk.Frame(parent, bg=COL["card"], highlightthickness=1, highlightbackground=COL["line"])
        table_frame.grid(row=5, column=0, sticky="nsew")
        columns = ("cible", "rang", "attaque", "k", "avant", "apres", "gain", "ratio", "iter")
        self.table = ttk.Treeview(table_frame, columns=columns, show="headings", height=10)
        setup = {
            "cible": ("cible", 48),
            "rang": ("rang", 38),
            "attaque": ("attaque", 56),
            "k": ("k", 30),
            "avant": ("avant", 50),
            "apres": ("après", 50),
            "gain": ("gain", 50),
            "ratio": ("ratio", 42),
            "iter": ("it.", 30),
        }
        for col, (label, width) in setup.items():
            self.table.heading(col, text=label)
            self.table.column(col, width=width, anchor=tk.CENTER, stretch=True)
        scroll = ttk.Scrollbar(table_frame, orient=tk.VERTICAL, command=self.table.yview)
        self.table.configure(yscrollcommand=scroll.set)
        self.table.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)

    def _button(self, parent: tk.Widget, text: str, command, variant: str = "ghost") -> tk.Button:
        if variant == "primary":
            bg, fg, active = COL["dark"], "#ffffff", "#1d2a36"
        elif variant == "selected":
            bg, fg, active = COL["navy"], "#ffffff", COL["navy_dark"]
        else:
            bg, fg, active = COL["panel"], COL["text"], COL["blue_soft"]
        return tk.Button(parent, text=text, command=command, bg=bg, fg=fg, activebackground=active, activeforeground=fg, relief=tk.FLAT, bd=0, padx=10, pady=7, font=("Segoe UI", 9, "bold"), cursor="hand2")

    def _segmented(self, parent: tk.Frame, row: int, label: str, var: tk.StringVar, values, command) -> int:
        tk.Label(parent, text=label, bg=COL["card"], fg=COL["text"], font=("Segoe UI", 9, "bold")).grid(row=row, column=0, sticky="w", pady=(5, 3))
        frame = tk.Frame(parent, bg=COL["panel"], highlightthickness=1, highlightbackground=COL["line"], padx=4, pady=4)
        frame.grid(row=row + 1, column=0, sticky="ew")
        buttons: list[tk.Button] = []

        def choose(value: str) -> None:
            var.set(value)
            self._refresh_segmented(var, buttons)
            command()

        for i, value in enumerate(values):
            frame.grid_columnconfigure(i, weight=1)
            btn = self._button(frame, value, lambda v=value: choose(v), "ghost")
            btn.grid(row=0, column=i, sticky="ew", padx=2)
            buttons.append(btn)
        self._option_groups.append((var, buttons))
        self._refresh_segmented(var, buttons)
        return row + 2

    def _refresh_segmented(self, var: tk.StringVar, buttons: list[tk.Button]) -> None:
        for btn in buttons:
            selected = btn.cget("text") == var.get()
            btn.configure(bg=COL["navy"] if selected else COL["panel"], fg="#ffffff" if selected else COL["text"], activebackground=COL["navy_dark"] if selected else COL["blue_soft"])

    def _slider(self, parent: tk.Frame, row: int, label: str, var: tk.Variable, min_value: float, max_value: float, fmt: str, integer: bool = False) -> int:
        header = tk.Frame(parent, bg=COL["card"])
        header.grid(row=row, column=0, sticky="ew", pady=(5, 2))
        header.grid_columnconfigure(0, weight=1)
        tk.Label(header, text=label, bg=COL["card"], fg=COL["text"], font=("Segoe UI", 9, "bold")).grid(row=0, column=0, sticky="w")
        value_label = tk.Label(header, text=fmt.format(float(var.get())), bg=COL["card"], fg=COL["muted"], font=("Segoe UI", 9))
        value_label.grid(row=0, column=1, sticky="e")
        canvas = tk.Canvas(parent, height=19, bg=COL["card"], highlightthickness=0, cursor="hand2")
        canvas.grid(row=row + 1, column=0, sticky="ew")

        def ratio() -> float:
            return (float(var.get()) - min_value) / (max_value - min_value)

        def redraw(_event=None) -> None:
            canvas.delete("all")
            width = max(80, canvas.winfo_width())
            x1, x2, y = 8, width - 8, 10
            pos = x1 + (x2 - x1) * max(0, min(1, ratio()))
            canvas.create_line(x1, y, x2, y, fill=COL["line"], width=4, capstyle=tk.ROUND)
            canvas.create_line(x1, y, pos, y, fill=COL["navy"], width=4, capstyle=tk.ROUND)
            canvas.create_oval(pos - 6, y - 6, pos + 6, y + 6, fill=COL["card"], outline=COL["navy"], width=3)
            value_label.configure(text=fmt.format(float(var.get())))

        def set_from_x(x: float) -> None:
            width = max(80, canvas.winfo_width())
            pos = max(0, min(1, (x - 8) / (width - 16)))
            value = min_value + pos * (max_value - min_value)
            var.set(int(round(value)) if integer else round(value, 2))
            redraw()
            self._programmer_recalcul(430)

        canvas.bind("<Configure>", redraw)
        canvas.bind("<Button-1>", lambda event: set_from_x(event.x))
        canvas.bind("<B1-Motion>", lambda event: set_from_x(event.x))
        redraw()
        return row + 2

    def _legend(self, parent: tk.Frame, row: int) -> None:
        frame = tk.Frame(parent, bg=COL["card"])
        frame.grid(row=row, column=0, sticky="ew", pady=(6, 0))
        frame.grid_columnconfigure(0, weight=1)
        frame.grid_columnconfigure(1, weight=1)
        tk.Label(frame, text="Légende", bg=COL["card"], fg=COL["text"], font=("Segoe UI", 10, "bold")).grid(row=0, column=0, columnspan=2, sticky="w")
        items = (
            ("●", COL["blue"], "page"),
            ("●", COL["orange"], "attaquant"),
            ("◆", COL["red"], "cible"),
            ("→", COL["blue"], "lien ajouté"),
            ("▰", COL["green"], "gain PageRank"),
        )
        for index, (symbol, color, text) in enumerate(items):
            line = tk.Frame(frame, bg=COL["card"])
            line.grid(row=1 + index // 2, column=index % 2, sticky="w", pady=(2, 0))
            tk.Label(line, text=symbol, bg=COL["card"], fg=color, font=("Segoe UI", 10, "bold"), width=2).pack(side=tk.LEFT)
            tk.Label(line, text=text, bg=COL["card"], fg=COL["muted"], font=("Segoe UI", 8)).pack(side=tk.LEFT)

    def _pause(self) -> None:
        self.animation = not self.animation
        self.pause_button.configure(text="▶  Reprendre" if not self.animation else "⏸  Pause")

    def _set_phase(self, phase: str) -> None:
        self.phase.set(phase)
        self._refresh_phase_buttons()
        self._dessiner()

    def _refresh_phase_buttons(self) -> None:
        order = ("initial", "attaquants", "impact", "resultats")
        current = order.index(self.phase.get()) if self.phase.get() in order else 2
        for i, phase in enumerate(order):
            active = i <= current
            self._phase_buttons[phase].configure(bg=COL["navy"] if active else COL["panel"], fg="#ffffff" if active else COL["muted"], activebackground=COL["navy_dark"] if active else COL["blue_soft"])

    def _changer_modele(self) -> None:
        choice = self.modele.get()
        try:
            if choice == "Pédagogique":
                self.graphe = creer_graphe_demo()
            elif choice == "Harvard500":
                self.graphe = lire_graphe(HARVARD_PAR_DEFAUT)
            else:
                self._choisir_fichier()
                return
            self.cache_base.clear()
            self.cache_apercu.clear()
            self._programmer_recalcul(0)
        except Exception as exc:
            messagebox.showerror("Erreur de chargement", str(exc))

    def _choisir_fichier(self) -> None:
        path = filedialog.askopenfilename(title="Choisir un graphe", initialdir=str(RACINE_PROJET), filetypes=[("Fichiers texte", "*.txt"), ("Tous les fichiers", "*.*")])
        if not path:
            return
        try:
            self.chemin_fichier = Path(path)
            self.graphe = lire_graphe(self.chemin_fichier)
            self.modele.set("Fichier")
            for var, buttons in self._option_groups:
                if var is self.modele:
                    self._refresh_segmented(var, buttons)
            self.cache_base.clear()
            self.cache_apercu.clear()
            self._programmer_recalcul(0)
        except Exception as exc:
            messagebox.showerror("Erreur de chargement", str(exc))

    def _programmer_recalcul(self, delay: int = 320) -> None:
        if self._after_recalcul is not None:
            self.after_cancel(self._after_recalcul)
        self._after_recalcul = self.after(delay, self._lancer_recalcul)

    def _lancer_recalcul(self) -> None:
        self._after_recalcul = None
        if self._calcul_en_cours:
            self._recalcul_en_attente = True
            return
        self._calcul_en_cours = True
        self._requete += 1
        request_id = self._requete
        graph = self.graphe
        alpha = float(self.alpha.get())
        k = max(1, int(self.k.get()))
        k_max = max(1, int(self.k_max.get()))
        type_cible = self.type_cible.get()
        type_attaque = self.type_attaque.get()
        self.status_chip.configure(text="calcul", bg=COL["orange_soft"], fg=COL["orange"])
        self._status("Calcul PageRank en arrière-plan...")

        def worker() -> None:
            try:
                result, scores, cibles, base_iter = self._calculer_resultat(graph, alpha, k, type_cible, type_attaque)
                rows = self._calculer_apercu(graph, alpha, scores, cibles, base_iter, k_max)
                self.after(0, lambda: self._appliquer_resultat(request_id, result, scores, cibles, rows))
            except Exception as exc:
                self.after(0, lambda: self._status(f"Erreur : {exc}"))
            finally:
                self.after(0, self._terminer_recalcul)

        threading.Thread(target=worker, daemon=True).start()

    def _terminer_recalcul(self) -> None:
        self._calcul_en_cours = False
        if self._recalcul_en_attente:
            self._recalcul_en_attente = False
            self._programmer_recalcul(90)

    def _base(self, graph, alpha: float) -> tuple[list[float], dict, int]:
        key = (id(graph), round(alpha, 6))
        if key not in self.cache_base:
            scores, iterations = pagerank_base(graph, alpha)
            self.cache_base[key] = (scores, selectionner_cibles(scores), iterations)
        return self.cache_base[key]

    def _calculer_resultat(self, graph, alpha: float, k: int, type_cible: str, type_attaque: str):
        scores, cibles, base_iter = self._base(graph, alpha)
        cible = cibles[type_cible]
        score_apres, iter_attaque = pagerank_attaque(graph, scores, cible.page, k, type_attaque, alpha)
        gain = score_apres - cible.score_initial
        ratio = score_apres / cible.score_initial if cible.score_initial > 0 else 0.0
        return (
            ResultatSimulation(graph.nom, graph.n, graph.m, alpha, k, type_cible, type_attaque, cible.page + 1, cible.rang, cible.score_initial, score_apres, gain, ratio, base_iter, iter_attaque),
            scores,
            cibles,
            base_iter,
        )

    def _calculer_apercu(self, graph, alpha: float, scores: list[float], cibles: dict, base_iter: int, k_max: int):
        key = (id(graph), round(alpha, 6), max(1, min(k_max, 200)))
        if key in self.cache_apercu:
            return self.cache_apercu[key]
        rows = []
        valeurs_k = k_demandes(key[2])
        for type_cible in CIBLES:
            cible = cibles[type_cible]
            for type_attaque in ATTAQUES:
                for k in valeurs_k:
                    score, iters = pagerank_attaque(graph, scores, cible.page, k, type_attaque, alpha)
                    ratio = score / cible.score_initial if cible.score_initial > 0 else 0.0
                    rows.append((type_cible, cible.rang, type_attaque, k, cible.score_initial, score, score - cible.score_initial, ratio, iters, base_iter))
        self.cache_apercu[key] = rows
        return rows

    def _appliquer_resultat(self, request_id: int, result, scores, cibles, rows) -> None:
        if request_id != self._requete:
            return
        self.resultat = result
        self.scores_base = scores
        self.cibles = cibles
        self.status_chip.configure(text="prêt", bg=COL["green_soft"], fg=COL["green"])
        self._metrics()
        self._event()
        self._table(rows)
        self._dessiner()

    def _status(self, text: str) -> None:
        self.event.delete("1.0", tk.END)
        self.event.insert(tk.END, text)

    def _metrics(self) -> None:
        r = self.resultat
        if not r:
            return
        for key, value in {"alpha": f"{r.alpha:.2f}", "k": str(r.k), "avant": f"{r.pagerank_initial:.5f}", "apres": f"{r.pagerank_apres:.5f}", "ratio": f"x{r.ratio:.2f}", "gain": f"{r.augmentation:+.5f}"}.items():
            self.metrics[key].configure(text=value)

    def _event(self) -> None:
        r = self.resultat
        if not r:
            return
        text = "\n\n".join(
            [
                f"Graphe : {r.graphe} ({r.n} pages, {r.m} liens).",
                f"Cible {r.type_cible} : page {r.page_cible}, rang initial {r.rang_initial}.",
                f"Attaque : {etiquette_attaque(r.type_attaque).lower()}, k={r.k}.",
                f"PageRank : {r.pagerank_initial:.8f} → {r.pagerank_apres:.8f}.",
                f"Ratio : x{r.ratio:.2f}, itérations : {r.iterations_attaque}.",
            ]
        )
        self.event.delete("1.0", tk.END)
        self.event.insert(tk.END, text)

    def _table(self, rows) -> None:
        for item in self.table.get_children():
            self.table.delete(item)
        self.results_title.configure(text=f"Résultats demandés ({len(rows)} lignes)")
        for type_cible, rang, type_attaque, k, before, after, gain, ratio, iters, _base_iter in rows:
            self.table.insert("", tk.END, values=(type_cible, rang, type_attaque, k, f"{before:.4f}", f"{after:.4f}", f"{gain:+.4f}", f"x{ratio:.2f}", iters))

    def _boucle_animation(self) -> None:
        if self.animation and self.resultat is not None:
            self.tick += 1
            self._dessiner()
        self.after(680, self._boucle_animation)

    def _dessiner(self) -> None:
        c = self.canvas
        c.delete("all")
        w = max(520, c.winfo_width())
        h = max(360, c.winfo_height())
        r = self.resultat
        if not r:
            c.create_text(w / 2, h / 2, text="Calcul en cours...", fill=COL["muted"], font=("Segoe UI", 14, "bold"))
            return

        center_y = min(max(145, h * 0.49), h - 118)
        web_radius = max(42, min(82, w * 0.11, center_y - 74, h - center_y - 78))
        att_radius = max(36, min(66, web_radius * 0.78))
        target_r = max(30, min(38, web_radius * 0.48))
        left_x = max(web_radius + 34, w * 0.24)
        attacker_x = w * 0.50
        target_x = min(w - target_r - 46, w * 0.74)
        web = (left_x, center_y)
        attackers = (attacker_x, center_y)
        target = (target_x, center_y)
        self._draw_web(c, web, web_radius)
        self._draw_target(c, target, target_r, r)
        if self.phase.get() in {"attaquants", "impact", "resultats"}:
            self._draw_attackers(c, attackers, att_radius, target, target_r, r)
        if self.phase.get() in {"impact", "resultats"}:
            self._draw_gain(c, target, target_r, r)
        if self.phase.get() == "resultats":
            self._draw_result_box(c, w, h, r)

    def _draw_web(self, c: tk.Canvas, center: tuple[float, float], radius: float) -> None:
        n = min(self.graphe.n, 14)
        points = []
        for i in range(n):
            angle = -math.pi / 2 + 2 * math.pi * i / n
            points.append((center[0] + math.cos(angle) * radius, center[1] + math.sin(angle) * radius))
        for i, point in enumerate(points):
            for dest, _ in self.graphe.sorties[i][:2]:
                if dest < n:
                    self._line(c, point, points[dest], COL["line"], 1)
        target_index = self.resultat.page_cible - 1 if self.resultat else -1
        for i, point in enumerate(points):
            fill, outline = COL["blue_soft"], COL["blue"]
            if i == target_index and self.graphe.n <= n:
                fill, outline = COL["red_soft"], COL["red"]
            self._circle(c, point, 14, fill, outline, 2)
            c.create_text(*point, text=str(i + 1), fill=COL["text"], font=("Segoe UI", 8, "bold"))
        c.create_text(center[0], center[1] + radius + 32, text="graphe web", fill=COL["muted"], font=("Segoe UI", 10, "bold"))

    def _draw_target(self, c: tk.Canvas, center: tuple[float, float], radius: float, r: ResultatSimulation) -> None:
        self._circle(c, center, radius, COL["red_soft"], COL["red"], 3)
        self._circle(c, center, radius * 0.56, "#fff8f5", COL["red"], 2)
        self._circle(c, center, radius * 0.20, COL["red"], COL["red"], 1)
        c.create_text(center[0], center[1] + radius + 23, text=etiquette_cible(r.type_cible), fill=COL["text"], font=("Segoe UI", 10, "bold"))
        c.create_text(center[0], center[1] + radius + 41, text=f"page {r.page_cible}", fill=COL["muted"], font=("Segoe UI", 8))

    def _draw_attackers(self, c: tk.Canvas, center: tuple[float, float], radius: float, target: tuple[float, float], target_r: float, r: ResultatSimulation) -> None:
        visible = min(r.k, 8)
        pulse = 1 + 0.014 * math.sin(self.tick / 3)
        points = []
        for i in range(visible):
            angle = -math.pi / 2 + 2 * math.pi * i / visible
            points.append((center[0] + math.cos(angle) * radius * pulse, center[1] + math.sin(angle) * radius * pulse))
        if r.type_attaque == "isoles":
            for p in points:
                self._arrow(c, p, target, COL["blue"], target_r)
        elif r.type_attaque == "complet":
            for i, p1 in enumerate(points):
                for p2 in points[i + 1:]:
                    self._line(c, p1, p2, "#bfd3e4", 1)
                self._arrow(c, p1, target, COL["blue"], target_r)
        elif points:
            self._arrow(c, target, points[0], COL["red"], 16)
            for i, p in enumerate(points):
                self._arrow(c, p, target if i == len(points) - 1 else points[i + 1], COL["blue"], target_r if i == len(points) - 1 else 16)
        node_r = max(10, min(16, radius * 0.17))
        for i, p in enumerate(points):
            self._circle(c, p, node_r, COL["orange_soft"], COL["orange"], 2)
            c.create_text(*p, text=f"A{i + 1}", fill=COL["text"], font=("Segoe UI", 8, "bold"))
        if r.k > visible:
            c.create_text(center[0] + radius + 22, center[1] + radius * 0.75, text=f"+ {r.k - visible}", fill=COL["orange"], font=("Segoe UI", 10, "bold"))
        c.create_text(center[0], center[1] + radius + 34, text=etiquette_attaque(r.type_attaque), fill=COL["muted"], font=("Segoe UI", 9, "bold"))

    def _draw_gain(self, c: tk.Canvas, center: tuple[float, float], target_r: float, r: ResultatSimulation) -> None:
        gain = min(1, max(0, (r.ratio - 1) / 10))
        radius = target_r + 10 + 24 * gain + 1.5 * math.sin(self.tick / 3)
        self._circle(c, center, radius, "", COL["green"], 3)
        c.create_text(center[0], center[1] - radius - 20, text=f"x{r.ratio:.2f}", fill=COL["green"], font=("Segoe UI", 17, "bold"))

    def _draw_result_box(self, c: tk.Canvas, w: int, h: int, r: ResultatSimulation) -> None:
        x0, y0 = max(20, w * 0.34), h - 88
        c.create_rectangle(x0, y0, w - 20, h - 18, fill=COL["panel"], outline=COL["line"])
        c.create_text(x0 + 14, y0 + 18, anchor=tk.W, text="format résultat demandé", fill=COL["text"], font=("Segoe UI", 10, "bold"))
        c.create_text(x0 + 14, y0 + 42, anchor=tk.W, text=f"{r.type_cible}, {r.type_attaque}, k={r.k}, PR={r.pagerank_apres:.8f}", fill=COL["muted"], font=("Segoe UI", 10))
        c.create_text(x0 + 14, y0 + 62, anchor=tk.W, text=f"gain {r.augmentation:+.8f}, ratio x{r.ratio:.3f}", fill=COL["green"], font=("Segoe UI", 10, "bold"))

    def _circle(self, c: tk.Canvas, center, radius, fill, outline, width) -> None:
        x, y = center
        opts = {"outline": outline, "width": width}
        if fill:
            opts["fill"] = fill
        c.create_oval(x - radius, y - radius, x + radius, y + radius, **opts)

    def _line(self, c: tk.Canvas, p1, p2, color, width) -> None:
        c.create_line(p1[0], p1[1], p2[0], p2[1], fill=color, width=width)

    def _arrow(self, c: tk.Canvas, p1, p2, color, end_margin: float = 42) -> None:
        x1, y1 = p1
        x2, y2 = p2
        dx, dy = x2 - x1, y2 - y1
        dist = max(1, math.hypot(dx, dy))
        sx, sy = x1 + dx * 18 / dist, y1 + dy * 18 / dist
        ex, ey = x2 - dx * end_margin / dist, y2 - dy * end_margin / dist
        c.create_line(sx, sy, ex, ey, fill=color, width=2, arrow=tk.LAST, arrowshape=(12, 14, 5))

    def _exporter(self) -> None:
        if self._export_en_cours:
            return
        self._export_en_cours = True
        self._status("Export CSV en cours...")
        graph = self.graphe
        alpha = float(self.alpha.get())
        k_max = max(1, int(self.k_max.get()))

        def worker() -> None:
            try:
                paths = generer_exports(graph, alpha=alpha, k_max=k_max, pas=None, dossier=DOSSIER_DEMO / "exports")
                self.after(0, lambda: self._export_done(paths))
            except Exception as exc:
                self.after(0, lambda: messagebox.showerror("Export impossible", str(exc)))
            finally:
                self._export_en_cours = False

        threading.Thread(target=worker, daemon=True).start()

    def _export_done(self, paths: dict[str, Path]) -> None:
        lines = ["Export terminé."]
        for label, path in paths.items():
            lines.append(f"{label}: {path}")
        self.event.delete("1.0", tk.END)
        self.event.insert(tk.END, "\n".join(lines))
        messagebox.showinfo("Export terminé", "Les CSV et la synthèse ont été générés.")


if __name__ == "__main__":
    app = GoogleBombingApp()
    app.mainloop()
