#ifndef NAMEDAYS_CZ_H
#define NAMEDAYS_CZ_H
// clang-format off

/*-- Tabela imienin -- Imiona dla każdego dnia roku rozdzielone przecinkami.
Jeśli w danym dniu występuje wiele imion, należy pozostawić między nimi spację, a
program będzie je zamieniał co 4 sekundy.
*/

  // Tablica imienin - imiona oddzielone przecinkami na każdy dzień roku (rotacja co 4 sekundy) - źródło: kalendarzswiat.pl
const char* namedays[] PROGMEM = {
// Leden (31 dní)
"Nový rok", "Karina,Abel", "Radmila", "Diana", "Dalimil", "Tři králové", "Vilma", "Čestmír", "Vladan", "Břetislav",
"Bohdana", "Pravoslav", "Edita", "Radovan", "Alice", "Ctirad", "Drahoslav", "Vladislav", "Doubravka", "Ilona",
"Běla", "Slavomír", "Zdeněk", "Milena", "Miloš", "Zora", "Ingrid", "Otýlie", "Zdislava", "Robin", "Marika",
// Únor (29 dní - přestupný rok)
"Hynek", "Nela", "Blažej", "Jarmila", "Dobromila", "Vanda", "Veronika", "Milada", "Apolena", "Mojmír",
"Božena", "Slavěna", "Věnceslav", "Valentýn", "Jiřina", "Ljuba", "Miloslava", "Gizela", "Patrik", "Oldřich",
"Lenka", "Petr", "Svatopluk", "Matěj", "Liliana", "Dorota", "Alexandr", "Lumír", "Horymír",
// Březen (31 dní)
"Bedřich", "Anežka", "Kamil", "Stela", "Kazimír", "Miroslav", "Tomáš", "Gabriela", "Františka", "Viktorie",
"Anděla", "Řehoř", "Růžena", "Rút", "Ida", "Elena", "Vlastimil", "Eduard", "Josef", "Světlana",
"Radek", "Leona", "Ivona", "Gabriel", "Marián", "Emanuel", "Dita", "Soňa", "Taťána", "Arnošt", "Kvido",
// Duben (30 dní)
"Hugo", "Erika", "Richard", "Ivana", "Miroslava", "Vendula", "Heřman,Hermína", "Ema", "Dušan", "Darja",
"Izabela", "Julius", "Aleš", "Vincenc", "Anastázie", "Irena", "Rudolf", "Valérie", "Rostislav", "Marcela",
"Alexandra", "Evženie", "Vojtěch", "Jiří", "Marek", "Oto", "Jaroslav", "Vlastislav", "Robert", "Blahoslav",
// Květen (31 dní)
"", "Zikmund", "Alexej", "Květoslav", "Klaudie", "Radoslav", "Stanislav", "Denisa", "Ctibor", "Blažena",
"Svatava", "Pankrác", "Servác", "Bonifác", "Žofie", "Přemysl", "Aneta", "Nataša", "Ivo", "Zbyšek",
"Monika", "Emil", "Vladimír", "Jana", "Viola", "Filip", "Valdemar", "Vilém", "Maxmilián", "Ferdinand", "Kamila",
// Červen (30 dní)
"Laura", "Jarmil", "Tamara", "Dalibor", "Dobroslav", "Norbert", "Iveta,Slavoj", "Medard", "Stanislava", "Gita",
"Bruno", "Antonie", "Antonín", "Roland", "Vít", "Zbyněk", "Adolf", "Milan", "Leoš", "Květa",
"Alois", "Pavla", "Zdeňka", "Jan", "Ivan", "Adriana", "Ladislav", "Lubomír", "Petr a Pavel", "Šárka",
// Červenec (31 dní)
"Jaroslava", "Patricie", "Radomír", "Prokop", "Cyril a Metoděj", "Milada", "Bohuslava", "Nora", "Drahoslava", "Libuše",
"Olga", "Bořek", "Markéta", "Karina", "Jindřich", "Luboš", "Martina", "Drahomíra", "Čeněk", "Ilja",
"Vítězslav", "Magdaléna", "Libor", "Kristýna", "Jakub", "Anna", "Věroslav", "Viktor", "Marta", "Bořivoj", "Ignác",
// Srpen (31 dní)
"Oskar", "Gustav", "Miluše", "Dominik", "Kristián", "Oldřiška", "Lada", "Soběslav", "Roman", "Vavřinec",
"Zuzana", "Klára", "Alena", "Alan", "Hana", "Jáchym", "Petra", "Helena", "Ludvík", "Bernard",
"Johana", "Bohuslav", "Sandra", "Bartoloměj", "Radim", "Luděk", "Otakar", "Augustýn", "Evelína", "Vladěna", "Pavlína",
// Září (30 dní)
"Linda,Samuel", "Adéla", "Bronislav", "Jindřiška", "Boris", "Boleslav", "Regína", "Mariana", "Daniela", "Irma",
"Denisa", "Marie", "Lubor", "Radka", "Jolana", "Ludmila", "Naděžda", "Kryštof", "Zita", "Oleg",
"Matouš", "Darina", "Berta", "Jaromír", "Zlata", "Andrea", "Jonáš", "Václav", "Michal", "Jeroným",
// Říjen (31 dní)
"Igor", "Olívie", "Bohumil", "Františka", "Eliška", "Hanuš", "Justýna", "Věra", "Štefan", "Marina",
"Anděla", "Marcel", "Renáta", "Agáta", "Tereza", "Havel", "Hedvika", "Lukáš", "Michaela", "Vendelín",
"Brigita", "Sabina", "Teodor", "Nina", "Beáta", "Erik", "Šarlota", "Silvie", "", "Tadeáš", "Štěpánka",
// Listopad (30 dní)
"Felix", "", "Hubert", "Karel", "Miriam", "Liběna", "Saskie", "Bohumír", "Bohdan", "Evžen",
"Martin", "Benedikt", "Tibor", "Sáva", "Leopold", "Otmar", "Mahulena", "Romana", "Alžběta", "Nikola",
"Albert", "Cecílie", "Klement", "Emílie", "Kateřina", "Artur", "Xenie", "René", "Zina", "Ondřej",
// Prosinec (31 dní)
"Iva", "Blanka", "Svatoslav", "Barbora", "Jitka", "Mikuláš", "Ambrož", "Květoslava", "Vratislav", "Julie",
"Dana", "Simona", "Lucie", "Lýdie", "Radana", "Albína", "Daniel", "Miloslava", "Ester", "Dagmar",
"Natálie", "Šimon", "Vlasta", "Štěpán", "Štěpán", "Štěpán", "Žaneta", "Bohumila", "Judita", "David", "Silvestr"
};


#endif // NAMEDAYS_PL_H
// clang-format on
