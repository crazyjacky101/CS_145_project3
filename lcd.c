// [Number] - [Title]   TOP

//A- song select
//B - stop/start
//C- TEMP?
//D- PITCH?


#include "avr.h"
#include "lcd.h"

#define DDR    DDRB
#define PORT   PORTB
#define RS_PIN 0 //PB0
#define RW_PIN 1 //PB1
#define EN_PIN 2 //PB2

#include <stdbool.h>
#include <stdio.h>


static inline void
set_data(unsigned char x)
{
	PORTD = x;
	DDRD = 0xff;
}

static inline unsigned char
get_data(void)
{
	DDRD = 0x00;
	return PIND;
}

static inline void
sleep_700ns(void)
{
	NOP();
	NOP();
	NOP();
}

static unsigned char
input(unsigned char rs)
{
	unsigned char d;
	if (rs) SET_BIT(PORT, RS_PIN); else CLR_BIT(PORT, RS_PIN);
	SET_BIT(PORT, RW_PIN);
	get_data();
	SET_BIT(PORT, EN_PIN);
	sleep_700ns();
	d = get_data();
	CLR_BIT(PORT, EN_PIN);
	return d;
}

static void
output(unsigned char d, unsigned char rs)
{
	if (rs) SET_BIT(PORT, RS_PIN); else CLR_BIT(PORT, RS_PIN);
	CLR_BIT(PORT, RW_PIN);
	set_data(d);
	SET_BIT(PORT, EN_PIN);
	sleep_700ns();
	CLR_BIT(PORT, EN_PIN);
}

static void
write(unsigned char c, unsigned char rs)
{
	while (input(0) & 0x80);
	output(c, rs);
}

void
lcd_init(void)
{
	SET_BIT(DDR, RS_PIN);
	SET_BIT(DDR, RW_PIN);
	SET_BIT(DDR, EN_PIN);
	avr_wait(16);
	output(0x30, 0);
	avr_wait(5);
	output(0x30, 0);
	avr_wait(1);
	write(0x3c, 0);
	write(0x0c, 0);
	write(0x06, 0);
	write(0x01, 0);
}

void
lcd_clr(void)
{
	write(0x01, 0);
}

void
lcd_pos(unsigned char r, unsigned char c)
{
	unsigned char n = r * 40 + c;
	write(0x02, 0);
	while (n--) {
		write(0x14, 0);
	}
}

void
lcd_put(char c)
{
	write(c, 1);
}

void
lcd_puts1(const char *s)
{
	char c;
	while ((c = pgm_read_byte(s++)) != 0) {
		write(c, 1);
	}
}

void
lcd_puts2(const char *s)
{
	char c;
	while ((c = *(s++)) != 0) {
		write(c, 1);
	}
}



/*** data structures and helpers ***/

#define NUM_NOTES(song) (sizeof(song) / sizeof(song[0]))
#define WHOLE_DURATION   1000
#define HALF_DURATION    500
#define QUARTER_DURATION 250
#define EIGHTH_DURATION  125
int tempo_factor = 1;  // 1 = normal speed

static int last_played = -1;
const char* last_title = "No song playing";

int current_song_length = 0;
volatile bool should_stop = false;


typedef enum
{
	IDLE, PLAYING
} PlayState;

PlayState play_state = IDLE;


typedef enum
{
	A, As, B, C, Cs, D, Ds, Ee, F, Fs, G, Gs, Z
} Note;


typedef enum
{
	W, H, Q, Ei
} Duration;


typedef struct
{
	Note note;
	Duration duration;
} PlayingNote;


typedef struct
{
	PlayingNote *notes;
	const char *title;
	int length;
} SongList;


int get_duration_ms(Duration d)
{
	switch (d)
	{
		case W:  return WHOLE_DURATION / tempo_factor;
		case H:  return HALF_DURATION / tempo_factor;
		case Q:  return QUARTER_DURATION / tempo_factor;
		case Ei: return EIGHTH_DURATION / tempo_factor;
		default: return 250;
	}
}


float get_frequency(Note n)
{
	switch (n)
	{
		case A:  return (220.00)*2;
		case As: return (233.08)*2;
		case B:  return (246.94)*2;
		case C:  return (261.63)*2;
		case Cs: return (277.18)*2;
		case D:  return (293.66)*2;
		case Ds: return (311.13)*2;
		case Ee: return (329.63)*2;
		case F:  return (349.23)*2;
		case Fs: return (369.99)*2;
		case G:  return 392.00;
		case Gs: return 415.30;
		case Z: return 0.00;
		default: return 440.00;
	}
}


int get_frequency_period(Note n)
{
	if (n == Z)
	{
		return -1; // special for rest (since zero freq)
	} 
	
	float freq = get_frequency(n);
	return (int)(1000 / (2 * freq));  // in ms
}



static const char MAP[16] = {
	'1','2','3','A',
	'4','5','6','B',
	'7','8','9','C',
	'*','0','#','D'
};


PlayingNote shooting_stars[] = {
	{Ds, W},
	/* Wait for half */ {Z, H},
	{Ds, H},
	{Ee, H},
	/* Wait for half */ {Z, H},
	{B, Q},
	/* Wait for quarter */ {Z, Q},
	{Gs, Q}
	/* Keep going... */
};
PlayingNote STARS[] = {
	{A, H},
	/* Wait for half */
	{B, H},
	{C, H},
	/* Wait for half */
	{G, H},
	/* Wait for quarter */
	{A, H}
	/* Keep going... */
};

// list of all our songs
SongList songs[] =
{
	{ shooting_stars, "Shooting Stars", NUM_NOTES(shooting_stars) },
	{ STARS, "STARS", NUM_NOTES(STARS) }
};
SongList* current_song = NULL;
const int total_songs = sizeof(songs) / sizeof(songs[0]);




/*** keypad settings ***/

int is_pressed(int r, int c)
{
	DDRA = 0x00;
	PORTA = 0x00;

	SET_BIT(DDRA, r);
	CLR_BIT(PORTA, r);
	SET_BIT(PORTA, c + 4);

	avr_wait(5);
	if (GET_BIT(PINA, c + 4) == 0)
	{
		
		return 1;
	}
	return 0;
}


int keypad_get_key(void)
{
	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
		{
			if (is_pressed(i, j))
			{
				if (is_pressed(i, j))
				{
					while(is_pressed(i,j))
						; // wait until key released
					return i * 4 + j+1; // return index (0–15)
				}
			}
		}
	}
	return 0;
}


void display_no_song(void)
{
	lcd_clr();
	lcd_pos(0, 0);
	lcd_puts2("No song playing");
}


void set_song(void)
{
	int song_index = 0;
	char buf[17];

	while (1)
	{
		// Show current song
		lcd_clr();
		lcd_pos(0, 0);
		sprintf(buf, "%d - %s", song_index+1, songs[song_index].title);
		lcd_puts2(buf);
		lcd_pos(1, 0);
		lcd_puts2("A. Play  B. Next");

		int k;
		while ((k = keypad_get_key()) == -1);  // wait for key

		if (k == 4) // Save song selection
		{
			current_song = &songs[song_index];
			last_played = song_index;
			last_title = songs[song_index].title;
			current_song_length = songs[song_index].length;
			play_state = PLAYING;  // signal to main to start playing
			return; // return control to main
		}
		else if (k == 8)
		{
			song_index = (song_index + 1) % total_songs;
		}
	}
}


void play_note(const PlayingNote* note)
{
	int period = get_frequency_period(note->note); // in ms
	int duration = get_duration_ms(note->duration); // in ms
	int k = duration / (2 * period); // total number of ON+OFF cycles

	for (int i = 0; i < k; i++)
	{
		SET_BIT(PORTB, 3);   // speaker ON
		avr_wait(period);    // wait half a wave
		CLR_BIT(PORTB, 3);   // speaker OFF
		avr_wait(period);    // wait the other half
	}
}

void play_song(const PlayingNote song[], int length)
{
	should_stop = false; // reset stop flag
	
	for (int i = 0; i < length; i++)
	{
		if (should_stop)
		{
			break;
		}
		
		play_note(&song[i]);
		
	}
}




/** demo only settings to make sure LCD works**/
int main(void)
{
	
	avr_init();
	lcd_init();
	SET_BIT(DDRB, 3);

	
	lcd_clr();
	lcd_pos(0, 0);
	lcd_puts2("Loading.....");
	avr_wait(1000);
	
	
	while(1)
	{
		
		//  in PLAYING state , play current song
		if (play_state == PLAYING && current_song != NULL)
		{
			lcd_clr();
			lcd_pos(0, 0);
			lcd_puts2(current_song->title);
			lcd_pos(1, 0);
			lcd_puts2("playing...");

			play_song(current_song->notes, current_song->length);

			play_state = IDLE;
			display_no_song();
		}
		else if (play_state == IDLE && current_song == NULL)
		{
			display_no_song();
		}
		
		int k = keypad_get_key();
		if (k <= 0) continue; // ignore invalid keys
		
		if (k == 4)// A, select song mode
		{
			set_song();
		}
		else if (k == 8 && play_state == PLAYING) // Stopping
		{
			should_stop = true;
			play_state = IDLE;
			display_no_song();
		}
		else if (k == 8) // Starting
		{
			if (last_played != -1)
			{
				current_song = &songs[last_played];
				current_song_length = current_song->length;
				play_state = PLAYING;
			}
			else
			{
				lcd_clr();
				lcd_puts2("No song saved");
				avr_wait(1000);
				display_no_song();
			}
		}
	}
} 


/**
void play_note(const PlayingNote* note)
{
	int period = get_frequency_period(note->note); // in ms
	int duration = get_duration_ms(note->duration); // in ms
	int k = duration / (2 * period); // total number of ON+OFF cycles

	for (int i = 0; i < k; i++) 
	{
		SET_BIT(PORTB, 3);   // speaker ON
		avr_wait(period);    // wait half a wave
		CLR_BIT(PORTB, 3);   // speaker OFF
		avr_wait(period);    // wait the other half
	}
}
**/



/**
int main(void) 
{
	avr_init();
	lcd_init();
	SET_BIT(DDRB, 3); 
	
	lcd_clr();
	lcd_pos(0, 0);
	lcd_puts2("Loading.....");
	avr_wait(1000);

	while (1) 
	{
		lcd_clr();
		lcd_pos(0, 0);
		lcd_puts2("Shooting stars");
		lcd_pos(1, 0);
		lcd_puts2("playing...");
		
		play_song(shooting_stars, NUM_NOTES(shooting_stars));
		
		lcd_clr();
		avr_wait(1000); // optional delay between repeats
	}
}


int main(void)
{
	

	while (1)
	{
		for (int i = 0; i < 100; i++)
		{
			SET_BIT(PORTB, 3);
			avr_wait(1); 
			CLR_BIT(PORTB, 3);
			avr_wait(1); 
		}
		avr_wait(300); 

		for (int i = 0; i < 50; i++)
		{
			SET_BIT(PORTB, 3);
			avr_wait(3); 
			CLR_BIT(PORTB, 3);
			avr_wait(3); 
		}
		avr_wait(300); 
	}
}
**/