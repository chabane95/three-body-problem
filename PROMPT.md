# GPT-Prompt

Programmiere das ebene newtonsche Dreikörperproblem als GitHub-Projekt. Verwende C++17 für die Bewegungsgleichungen und Python für die Auswertung. Ich möchte die Bewegung sehen und nachvollziehen können, wie genau die Rechnung ist.

- Integriere drei frei wählbare Punktmassen mit Velocity-Verlet. Berechne die Kräfte paarweise. Gib Positionen, Geschwindigkeiten, Gesamtenergie, Impuls, Drehimpuls, Schwerpunkt und Paarabstände aus. Dokumentiere die Einheiten und den Umgang mit Kollisionen.
- Zeige die periodische Acht mit den publizierten Startwerten von Chenciner und Montgomery. Prüfe denselben Integrator an der exakten rotierenden Dreieckslösung von Lagrange. Miss den Positionsfehler über einen Umlauf und bestimme die Konvergenzordnung durch mehrere Schritthalbierungen.
- Erzeuge eine abspielbare Animation und drei Momentaufnahmen direkt aus den Simulationsdaten. In der Hauptansicht genügen Bahn, Zeit, Energieabweichung, Zeitregler und Wiedergabesteuerung. Weitere Messwerte sollen aufklappbar sein.
- Zeichne Trajektorien, Paarabstände, Energieabweichung und Konvergenz als PNG und Vektorgrafik. Orientiere dich an meinen Abbildungen in *Physical Review C* 111, 034913: weißer Hintergrund, gerahmte Achsen, nach innen gerichtete Ticks, kompakte Legenden. Verwende gleiche Maßstäbe für x und y. [Paper](https://journals.aps.org/prc/abstract/10.1103/PhysRevC.111.034913)
- Schreibe eine kurze deutsche README in natürlicher Sprache. Erst die Theorie und ihre Bedeutung, dann das Verfahren und die gemessene Genauigkeit. Liefere reproduzierbare Startbefehle, führe die Tests aus und zeige die fertige Animation.
