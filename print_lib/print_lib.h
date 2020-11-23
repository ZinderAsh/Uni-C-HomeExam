#ifndef PRINT_LIB_H
#define PRINT_LIB_H

void print_pkt( unsigned char* packet );
void print_received_pkt( short ownAddress, unsigned char* packet );
void print_forwardeg_pkt( short ownAddress, unsigned char* packet );
void print_weighted_edge( short from_node, short to_node, int weight );
void print_clear_logfile( void );

#endif
