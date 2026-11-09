#include "Lang.h"

#include <QByteArray>
#include <QHash>

namespace {
struct Row { const char* en; const char* pl; const char* de; const char* es; const char* it; const char* fr; };

// Nazwy wlasne i terminy branzowe (REC, PAUSE, STOP, SPLIT, Input, Output, Scheduler, CPU, GPU, Drop,
// Timecode, Autosave) sa wspolne dla wszystkich jezykow i nie maja wierszy.
const Row rows[] = {
    // --- naglowek ---
    {"Session", "Sesja", "Sitzung", "Sesión", "Sessione", "Session"},
    {"New session", "Nowa sesja", "Neue Sitzung", "Nueva sesión", "Nuova sessione", "Nouvelle session"},
    {"Recording time", "Czas nagrania", "Aufnahmezeit", "Tiempo de grabación", "Tempo di registrazione", "Durée d'enregistrement"},
    {"Recording time — channel %1", "Czas nagrania — kanał %1", "Aufnahmezeit — Kanal %1", "Tiempo de grabación — canal %1",
     "Tempo di registrazione — canale %1", "Durée d'enregistrement — canal %1"},
    {"Disk", "Dysk", "Laufwerk", "Disco", "Disco", "Disque"},
    {"%1 free of %2", "%1 wolne z %2", "%1 frei von %2", "%1 libres de %2", "%1 liberi su %2", "%1 libres sur %2"},
    {"Write", "Zapis", "Schreiben", "Escritura", "Scrittura", "Écriture"},
    {"HW enc.", "Enk. HW", "HW-Enc.", "Cod. HW", "Cod. HW", "Enc. mat."},
    {"View", "Widok", "Ansicht", "Vista", "Vista", "Vue"},
    {"List", "Lista", "Liste", "Lista", "Elenco", "Liste"},
    {"List view — from version %1", "Widok listy — od wersji %1", "Listenansicht — ab Version %1",
     "Vista de lista — desde la versión %1", "Vista elenco — dalla versione %1", "Vue en liste — à partir de la version %1"},
    // --- menu ---
    {"New list", "Nowa lista", "Neue Liste", "Nueva lista", "Nuovo elenco", "Nouvelle liste"},
    {"Load list…", "Wczytaj listę…", "Liste laden…", "Cargar lista…", "Carica elenco…", "Charger la liste…"},
    {"Save list", "Zapisz listę", "Liste speichern", "Guardar lista", "Salva elenco", "Enregistrer la liste"},
    {"Save as…", "Zapisz jako…", "Speichern unter…", "Guardar como…", "Salva con nome…", "Enregistrer sous…"},
    {"(from %1)", "(od %1)", "(ab %1)", "(desde %1)", "(dalla %1)", "(dès %1)"},
    {"Language", "Język", "Sprache", "Idioma", "Lingua", "Langue"},
    {"Diagnostics…", "Diagnostyka…", "Diagnose…", "Diagnóstico…", "Diagnostica…", "Diagnostic…"},
    {"Environment diagnostics", "Diagnostyka środowiska", "Umgebungsdiagnose", "Diagnóstico del entorno",
     "Diagnostica dell'ambiente", "Diagnostic de l'environnement"},
    {"About", "O programie", "Über", "Acerca de", "Informazioni", "À propos"},
    {"Multi-channel video recorder.", "Wielokanałowy rejestrator wideo.", "Mehrkanal-Videorekorder.",
     "Grabador de vídeo multicanal.", "Registratore video multicanale.", "Enregistreur vidéo multicanal."},
    // --- panel sterowania ---
    {"Selected channels (%1)", "Zaznaczone kanały (%1)", "Ausgewählte Kanäle (%1)", "Canales seleccionados (%1)",
     "Canali selezionati (%1)", "Canaux sélectionnés (%1)"},
    {"Selection", "Zaznaczenie", "Auswahl", "Selección", "Selezione", "Sélection"},
    {"SELECT ALL", "WSZYSTKIE", "ALLE", "TODOS", "TUTTI", "TOUS"},
    {"SELECT NONE", "ŻADEN", "KEINE", "NINGUNO", "NESSUNO", "AUCUN"},
    {"INVERT", "ODWRÓĆ", "UMKEHREN", "INVERTIR", "INVERTI", "INVERSER"},
    {"Active channel", "Kanał aktywny", "Aktiver Kanal", "Canal activo", "Canale attivo", "Canal actif"},
    {"MOVE UP", "W GÓRĘ", "NACH OBEN", "SUBIR", "SU", "MONTER"},
    {"MOVE DOWN", "W DÓŁ", "NACH UNTEN", "BAJAR", "GIÙ", "DESCENDRE"},
    {"Channel list", "Lista kanałów", "Kanalliste", "Lista de canales", "Elenco canali", "Liste des canaux"},
    {"ADD SOURCE", "DODAJ ŹRÓDŁO", "QUELLE HINZUFÜGEN", "AÑADIR FUENTE", "AGGIUNGI SORGENTE", "AJOUTER SOURCE"},
    {"LOAD", "WCZYTAJ", "LADEN", "CARGAR", "CARICA", "CHARGER"},
    {"SAVE", "ZAPISZ", "SPEICHERN", "GUARDAR", "SALVA", "ENREGISTRER"},
    {"From version %1", "Od wersji %1", "Ab Version %1", "Desde la versión %1", "Dalla versione %1", "À partir de la version %1"},
    {"Channels", "Kanały", "Kanäle", "Canales", "Canali", "Canaux"},
    {"PAUSED %1", "PAUZA %1", "PAUSE %1", "PAUSA %1", "PAUSA %1", "PAUSE %1"},
    {"WAITING %1", "CZEKA %1", "WARTET %1", "EN ESPERA %1", "IN ATTESA %1", "EN ATTENTE %1"},
    {"ERROR %1", "BŁĄD %1", "FEHLER %1", "ERROR %1", "ERRORE %1", "ERREUR %1"},
    // --- pasek stanu i komunikaty ---
    {"(from version %1)", "(od wersji %1)", "(ab Version %1)", "(desde la versión %1)", "(dalla versione %1)", "(à partir de la version %1)"},
    {"Limit of 16 channels reached — adding sources is disabled", "Limit 16 kanałów osiągnięty — dodawanie źródeł nieaktywne",
     "Limit von 16 Kanälen erreicht — Hinzufügen deaktiviert", "Límite de 16 canales alcanzado — no se pueden añadir fuentes",
     "Limite di 16 canali raggiunto — aggiunta disattivata", "Limite de 16 canaux atteinte — ajout désactivé"},
    {"Removed channel %1", "Usunięto kanał %1", "Kanal %1 entfernt", "Canal %1 eliminado", "Canale %1 rimosso", "Canal %1 supprimé"},
    {"Recording (Rec / Pause / Stop / Split) will be available from version %1",
     "Nagrywanie (Rec / Pause / Stop / Split) będzie dostępne od wersji %1",
     "Aufnahme (Rec / Pause / Stop / Split) ist ab Version %1 verfügbar",
     "La grabación (Rec / Pause / Stop / Split) estará disponible desde la versión %1",
     "La registrazione (Rec / Pause / Stop / Split) sarà disponibile dalla versione %1",
     "L'enregistrement (Rec / Pause / Stop / Split) sera disponible à partir de la version %1"},
    // --- siatka i kafel ---
    {"No channels", "Brak kanałów", "Keine Kanäle", "Sin canales", "Nessun canale", "Aucun canal"},
    {"ADD SOURCE adds a channel (max. 16)", "DODAJ ŹRÓDŁO dodaje kanał (maks. 16)", "QUELLE HINZUFÜGEN fügt einen Kanal hinzu (max. 16)",
     "AÑADIR FUENTE añade un canal (máx. 16)", "AGGIUNGI SORGENTE aggiunge un canale (max 16)", "AJOUTER SOURCE ajoute un canal (max. 16)"},
    {"ERROR", "BŁĄD", "FEHLER", "ERROR", "ERRORE", "ERREUR"},
    {"NO SIGNAL", "BRAK SYGNAŁU", "KEIN SIGNAL", "SIN SEÑAL", "NESSUN SEGNALE", "PAS DE SIGNAL"},
    {"RECONNECTING", "PONOWNE ŁĄCZENIE", "NEUVERBINDUNG", "RECONECTANDO", "RICONNESSIONE", "RECONNEXION"},
    {"Channel name", "Nazwa kanału", "Kanalname", "Nombre del canal", "Nome del canale", "Nom du canal"},
    {"Name:", "Nazwa:", "Name:", "Nombre:", "Nome:", "Nom :"},
    {"Test generator", "Generator testowy", "Testgenerator", "Generador de prueba", "Generatore di test", "Générateur de test"},
    {"cannot build the pipeline", "nie można zbudować pipeline'u", "Pipeline kann nicht erstellt werden",
     "no se puede crear el pipeline", "impossibile creare la pipeline", "impossible de créer le pipeline"},
    {"cannot start the preview", "nie można uruchomić podglądu", "Vorschau kann nicht gestartet werden",
     "no se puede iniciar la vista previa", "impossibile avviare l'anteprima", "impossible de démarrer l'aperçu"},
    {"pipeline error", "błąd pipeline'u", "Pipeline-Fehler", "error del pipeline", "errore della pipeline", "erreur du pipeline"},
    // --- inspektor ---
    {"Inspector", "Inspektor", "Inspektor", "Inspector", "Ispettore", "Inspecteur"},
    {"Channel %1 · %2", "Kanał %1 · %2", "Kanal %1 · %2", "Canal %1 · %2", "Canale %1 · %2", "Canal %1 · %2"},
    {"No active channel", "Brak aktywnego kanału", "Kein aktiver Kanal", "Sin canal activo", "Nessun canale attivo", "Aucun canal actif"},
    {"Source", "Źródło", "Quelle", "Fuente", "Sorgente", "Source"},
    {"Input type", "Typ wejścia", "Eingangstyp", "Tipo de entrada", "Tipo di ingresso", "Type d'entrée"},
    {"Screen grab", "Przechwytywanie ekranu", "Bildschirmaufnahme", "Captura de pantalla", "Cattura schermo", "Capture d'écran"},
    {"Pattern", "Wzór", "Muster", "Patrón", "Motivo", "Motif"},
    {"Video format", "Format wideo", "Videoformat", "Formato de vídeo", "Formato video", "Format vidéo"},
    {"%1 × %2, %3 fps", "%1 × %2, %3 kl./s", "%1 × %2, %3 fps", "%1 × %2, %3 fps", "%1 × %2, %3 fps", "%1 × %2, %3 i/s"},
    {"Audio signal", "Sygnał audio", "Audiosignal", "Señal de audio", "Segnale audio", "Signal audio"},
    {"Sine 440 Hz", "Sinus 440 Hz", "Sinus 440 Hz", "Seno 440 Hz", "Sinusoide 440 Hz", "Sinus 440 Hz"},
    {"Ticks every 1 s", "Tiki co 1 s", "Ticks alle 1 s", "Clics cada 1 s", "Tick ogni 1 s", "Tics toutes les 1 s"},
    {"Silence", "Cisza", "Stille", "Silencio", "Silenzio", "Silence"},
    {"SMPTE bars", "Pasy SMPTE", "SMPTE-Balken", "Barras SMPTE", "Barre SMPTE", "Mire SMPTE"},
    {"SMPTE bars 75%", "Pasy SMPTE 75%", "SMPTE-Balken 75%", "Barras SMPTE 75%", "Barre SMPTE 75%", "Mire SMPTE 75%"},
    {"SMPTE bars 100%", "Pasy SMPTE 100%", "SMPTE-Balken 100%", "Barras SMPTE 100%", "Barre SMPTE 100%", "Mire SMPTE 100%"},
    {"Ball", "Piłka", "Ball", "Pelota", "Palla", "Balle"},
    {"Pinwheel", "Wiatraczek", "Windrad", "Molinete", "Girandola", "Moulinet"},
    {"Spokes", "Szprychy", "Speichen", "Radios", "Raggi", "Rayons"},
    {"Circles", "Okręgi", "Kreise", "Círculos", "Cerchi", "Cercles"},
    {"Gradient", "Gradient", "Verlauf", "Degradado", "Gradiente", "Dégradé"},
    {"Snow", "Śnieg", "Rauschen", "Nieve", "Neve", "Neige"},
    {"Black", "Czarny", "Schwarz", "Negro", "Nero", "Noir"},
    {"Output — from version %1", "Output — od wersji %1", "Output — ab Version %1", "Output — desde la versión %1",
     "Output — dalla versione %1", "Output — à partir de la version %1"},
    {"Scheduler — from version %1", "Scheduler — od wersji %1", "Scheduler — ab Version %1", "Scheduler — desde la versión %1",
     "Scheduler — dalla versione %1", "Scheduler — à partir de la version %1"},
    {"Folder, name template, container, codecs and auto-split will arrive in version %1 together with recording.",
     "Folder, szablon nazwy, kontener, kodeki i auto-split pojawią się w wersji %1 razem z nagrywaniem.",
     "Ordner, Namensvorlage, Container, Codecs und Auto-Split kommen in Version %1 zusammen mit der Aufnahme.",
     "Carpeta, plantilla de nombre, contenedor, códecs y división automática llegarán en la versión %1 junto con la grabación.",
     "Cartella, modello di nome, contenitore, codec e divisione automatica arriveranno nella versione %1 insieme alla registrazione.",
     "Dossier, modèle de nom, conteneur, codecs et découpage automatique arriveront dans la version %1 avec l'enregistrement."},
    {"Start, stop and split scheduling will arrive in version %1.",
     "Harmonogram startu, stopu i splitu pojawi się w wersji %1.",
     "Zeitplanung für Start, Stopp und Split kommt in Version %1.",
     "La programación de inicio, parada y división llegará en la versión %1.",
     "La pianificazione di avvio, arresto e divisione arriverà nella versione %1.",
     "La planification du démarrage, de l'arrêt et du découpage arrivera dans la version %1."},
};

int g_lang = 0;   // indeks w available()

const QHash<QByteArray, const Row*>& table() {
    static const QHash<QByteArray, const Row*> t = [] {
        QHash<QByteArray, const Row*> h;
        for (const Row& r : rows) h.insert(QByteArray(r.en), &r);
        return h;
    }();
    return t;
}
}

const QList<Lang::Info>& Lang::available() {
    static const QList<Info> list{{"en", "English"}, {"pl", "Polski"}, {"de", "Deutsch"},
                                  {"es", "Español"}, {"it", "Italiano"}, {"fr", "Français"}};
    return list;
}

void Lang::set(const QString& code) {
    g_lang = 0;
    for (int i = 0; i < available().size(); ++i)
        if (code.compare(QLatin1String(available().at(i).code), Qt::CaseInsensitive) == 0) g_lang = i;
}

QString Lang::current() { return QLatin1String(available().at(g_lang).code); }

QString Lang::tr(const char* english) {
    if (g_lang != 0) {
        if (const Row* r = table().value(QByteArray(english))) {
            const char* const cols[] = {r->en, r->pl, r->de, r->es, r->it, r->fr};
            return QString::fromUtf8(cols[g_lang]);
        }
    }
    return QString::fromUtf8(english);
}
