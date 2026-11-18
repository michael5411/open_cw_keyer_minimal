# open_cw_keyer_minimal
Verwendet die Hardware von K3NG. Der Sketch ist auf die wesentlichen Funktionen ausgerichtet, die man für CW braucht und kommt mit 440 Codezeilen aus.

Folgende Anweisungen versteht der Sketch:

Memory 1 oder 2 belegen:	SET + P#

Buzzer ohne TX:		SET + X 

Buzzer und TX ansteuern:	SET + O

Buzzer aus, TX ansteuern:	SET + Y

Buzzer ein: 			SET + B

Die Zeichen P, X, O, Y und B werden in dem Tempo eingegeben, das mit dem Poti eingestellt wurde. Die SET-Taste wird gedrückt, dann die Anweisung per Morsezeichen eingegeben.  
Um einen CQ-Ruf im eingestellten Tempo ein zu geben, ist der Ablauf so:

1. SET-Taste drücken
2. Quittungston abwarten
3. P1 eingeben
4. Quittungston abwarten
5. SET-Taste drücken >> Die Morsesequenz ist gespeichert. 

Wenn ein Fehler aufgetreten ist, hört man bei 5.) einen tieferen Ton. Dann muss noch mal neu gestartet werden. In der Praxis ist dieser Ablauf einfach zu beherrschen.

Um einen Speicher auf Sendung zu bringen, reicht es aus, die Taste 1 oder 2 zu drücken. Zum Abbrechen drückt man die gleiche Taste noch einmal. 
Das Ausgabetempo wird auch dafür wieder am Poti gewählt.  Mit der Funktion SET + X hört man sich einen Speicherinhalt an, ohne zu senden. 
Mit dieser Funktion kann man auch recht gut Üben.  Mit SET + Y geht man auf Sendung. Hat der TX seinen eigenen Mithörton und möchte man nur diesen Ton hören, dann wählt man SET + Y. 
PTT wird im übrigen wie beim „großen Bruder“ geschaltet. Die Ausgänge sind mit Optokopplern ausgestattet.

