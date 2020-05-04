#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "base64.h"

#define HTTP_PORT 80
#define DEFAULT_LINE_LEN 255

/*
Because HTTP is line-oriented that is, clients are expected to pass in multiple CRLF-delimited lines
that describe a request you need a way to read a line from the connection. fgets is a standard way to
read a line of text from a file descriptor, including a socket, but it requires that you specify a
maximum line-length up front. Instead, develop a simple (and simplistic) routine that autoincrements
an internal buffer until it's read the entire line and returns it.
*/
char *read_line( int connection )
{
	static int 		line_len = DEFAULT_LINE_LEN;
	static char 		*line = NULL;
	int 				size;
	char 				c;    // must be c, not int
	int 				pos = 0;

	if ( !line ) {
		line = malloc( line_len );
	}

	while ((size = recv( connection, &c, 1, 0)) > 0) {
		if ( ( c == '\n' ) && ( line[ pos - 1 ] == '\r' ) ) {
		  line[ pos - 1 ] = '\0';
		  break;
		}

		line[ pos++ ] = c;

		if ( pos > line_len ) {
			line_len *= 2;
			line = realloc( line, line_len );
		}
	}

  return line;
}

static void build_success_response( int connection )
{
	char buf[ 255 ];
	sprintf(buf, "HTTP/1.1 200 Success\r\nConnection: Close\r\n Content-Type:text/html\r\n\
			\r\n<html><head><title>Test Page</title></head><body>Nothing here</body></html>\
			\r\n" );

	// Technically, this should account for short writes.
	if ( send( connection, buf, strlen( buf ), 0 ) < strlen( buf ) ) {
		perror( "Trying to respond" );
	}
}

static void build_error_response( int connection, int error_code )
{
	char buf[ 255 ];
	sprintf( buf, "HTTP/1.1 %d Error Occurred\r\n\r\n", error_code );

	// Technically, this should account for short writes.
	if ( send( connection, buf, strlen( buf ), 0 ) < strlen( buf ) ) {
		perror( "Trying to respond" );
	}
}

static void process_http_request( int connection )
{
  char 		*request_line;

  request_line = read_line( connection );
  if ( strncmp( request_line, "GET", 3 ) ) {
   // Only supports "GET" requests
    build_error_response( connection, 501 );
  } else {
   // Skip over all header lines, don't care
    while ( strcmp( read_line( connection ), "" ) );

    build_success_response( connection );
  }

  if ( close( connection ) == -1 ) {
    perror( "Unable to close connection" );
  }
}

int main( int argc, char *argv[ ] )
{
	int					listen_sock;
	int 					connect_sock;
	int					on = 1;
	struct sockaddr_in 	local_addr;
	struct sockaddr_in 	client_addr;
	int 					client_addr_len = sizeof( client_addr );

	if ( ( listen_sock = socket( PF_INET, SOCK_STREAM, 0 ) ) == -1 ) {
		perror( "Unable to create listening socket" );
		exit( 0 );
	}

	/*
	This enables the same process to be restarted if it terminates abnormally. Ordinarily,
	when a server process terminates abnormally, the socket is left open for a period of
	time referred to as the TIME_WAIT period. The socket is in TIME_WAIT state if you run
	netstat. This enables any pending client FIN packets to be received and processed correctly.
	Until this TIME_WAIT period has ended, no process can listen on the same port. SO_REUSEADDR
	enables a process to take up ownership of a socket that is in the TIME_WAIT state, so that
	on abnormal termination, the process can be immediately restarted. This is probably what you
	always want, but you have to ask for it explicitly.
	*/
	if ( setsockopt( listen_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof( on ) ) == -1 ) {
		perror( "Setting socket option" );
		exit( 0 );
	}

  local_addr.sin_family = AF_INET;
  local_addr.sin_port = htons( HTTP_PORT );
  local_addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
  //local_addr.sin_addr.s_addr = htonl( INADDR_ANY );

	if ( bind( listen_sock, ( struct sockaddr * ) &local_addr, sizeof( local_addr ) ) == -1 ) {
		perror( "Unable to bind to local address" );
		exit( 0 );
	}
  
  if ( listen( listen_sock, 5 ) == -1 ) {
    perror( "Unable to set socket backlog" );
    exit( 0 );
  }

	/*
	The accept call will block, not return until a client somewhere on the Internet calls connect
	with its IP and port number. 
	*/
	while ((connect_sock = accept(listen_sock, (struct sockaddr *) &client_addr, &client_addr_len)) != -1) {
		// TODO: ideally, this would spawn a new thread.
		process_http_request( connect_sock );
	}

  if ( connect_sock == -1 ) {
    perror( "Unable to accept socket" );
  }

  return 0;
}
