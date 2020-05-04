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
#include "tls.h"

#define HTTP_PORT    80
#define HTTPS_PORT      443

/**
 * Accept a well-formed URL (e.g. http://www.company.com/index.html) and return
 * pointers to the host part and the path part. Note that this function 
 * modifies the uri itself as well. It returns 0 on success, -1 if the URL is 
 * found to be malformed in any way.
 */
int parse_url( char *uri, char **host, char **path )
{
	char *pos;
	
	pos = strstr( uri, "//" );
	
	if ( !pos ) {
		return -1;
	}
	
	*host = pos + 2;
	
	pos = strchr( *host, '/' );
	
	if ( !pos ) {
		*path = NULL;
	} else {
		*pos = '\0';
		*path = pos + 1;
	}

  return 0;
}

int parse_proxy_param( char *proxy_spec,
            char **proxy_host,
            int *proxy_port,
            char **proxy_user,
            char **proxy_password )
{
  char *login_sep, *colon_sep, *trailer_sep;
  // Technically, the user should start the proxy spec with
  // "http://". But, be forgiving if he didn't.
  if ( !strncmp( "http://", proxy_spec, 7 ) )
  {
   proxy_spec += 7;
  } 
  login_sep = strchr( proxy_spec, '@' );
 
  if ( login_sep )
  {
    colon_sep = strchr( proxy_spec, ':' );
    if ( !colon_sep || ( colon_sep > login_sep ) )
    {
      // Error - if username supplied, password must be supplied.
      fprintf( stderr, "Expected password in '%s'\n", proxy_spec );
      return 0;
    }
    *colon_sep = '\0';
    *proxy_user = proxy_spec;
    *login_sep = '\0';
    *proxy_password = colon_sep + 1;
    proxy_spec = login_sep + 1;
  }
  // If the user added a "/" on the end (as they sometimes do),
  // just ignore it.
  trailer_sep = strchr( proxy_spec, '/' );
  if ( trailer_sep )
  {
    *trailer_sep = '\0';
  }

  colon_sep = strchr( proxy_spec, ':' );
  if ( colon_sep )
  {
    // non-standard proxy port
    *colon_sep = '\0';
    *proxy_host = proxy_spec;
    *proxy_port = atoi( colon_sep + 1 );
    if ( *proxy_port == 0 )
    {
     // 0 is not a valid port; this is an error, whether
     // it was mistyped or specified as 0.
     return 0;
    }
  }
  else
  { 
    *proxy_port = HTTP_PORT;
    *proxy_host = proxy_spec;
  }
  return 1;
}

#define MAX_GET_COMMAND 255
/**
 * Format and send an HTTP get command. The return value will be 0
 * on success, -1 on failure, with errno set appropriately. The caller
 * must then retrieve the response.
 */
int http_get(int connection, const char *path, const char *host, TLSParameters *tls_context)
{
	static char get_command[ MAX_GET_COMMAND ];
    char* proxy_host = NULL;

	/**
	In ordinary, "proxy-less" HTTP, you start by establishing a connection to the target HTTP host
	and then send in a GET /path HTTP/1.0 request line. However, when connecting to a proxy,
	you need to send a whole hostname because the socket itself has just been established between
	the client and the proxy.
	*/
	if ( proxy_host ) {
		sprintf( get_command, "GET http://%s/%s HTTP/1.1\r\n", host, path );
	} else {
		sprintf( get_command, "GET /%s HTTP/1.1\r\n", path );
	}

	if ( tls_send( connection, get_command, strlen( get_command ), 0, tls_context) == -1 ) {
		return -1;
	}

	/**
	The GET command itself is followed by a carriage-return/line-feed pair (0x0A 0x0D) and a colon-separated,
	CRLF-delimited list of headers that describe how the client wants the response to be returned.
	Only one header is required — the Host header, which is required to support virtual hosting, the situation
	where several hosts share one IP address or vice-versa.
	*/
	sprintf( get_command, "Host: %s\r\n", host );
	if ( tls_send( connection, get_command, strlen( get_command), 0, tls_context) == -1 ) {
		return -1;
	}

    /*
	if ( proxy_user ) {
		int credentials_len = strlen( proxy_user ) + strlen( proxy_password ) + 1;
		char *proxy_credentials = malloc( credentials_len );
		char *auth_string = malloc( ( ( credentials_len * 4 ) / 3 ) + 1 );
		sprintf( proxy_credentials, "%s:%s", proxy_user, proxy_password );
		base64_encode( proxy_credentials, credentials_len, auth_string );
		sprintf( get_command, "Proxy-Authorization: BASIC %s\r\n", auth_string );
		if ( send( connection, get_command, strlen( get_command ), 0 ) == -1 )
		{
			free( proxy_credentials );
			free( auth_string );
			return -1;
		}
		free( proxy_credentials );
		free( auth_string );
	}
    */

	/**
	The Connection header is not required, but in general you should send it to indicate to the client
	whether you want it to Keep-Alive the connection — if you plan on requesting more documents on this
	same socket or Close it. If you're just sending a single request and getting back a single response,
	it's easier to let the server just close the connection when it's done sending.
	The header list is terminated by an empty CRLF pair.
	*/
	sprintf( get_command, "Connection: close\r\n\r\n" );
	if (tls_send( connection, get_command, strlen( get_command), 0, tls_context) == -1 ) {
		return -1;
	}

	return 0;
}

#define BUFFER_SIZE 255

/**
 * Receive all data available on a connection and dump it to stdout
 */
void display_result( int connection, TLSParameters *tls_context)
{
  int 			received = 0;
  static char 	recv_buf[ BUFFER_SIZE + 1 ];

  while ((received = tls_recv(connection, recv_buf, BUFFER_SIZE, 0, tls_context)) > 0 ) {
    recv_buf[ received ] = '\0';
    printf( "%s", recv_buf );
  }
  printf( "\n" );
}

/**
 * Simple command-line HTTP client.
 */
int main( int argc, char *argv[ ] )
{
	int			    client_connection;
	char 		    *host, *path;
	char 		    *proxy_host;
	char		    *proxy_user;
	char		    *proxy_password;
	int			    proxy_port;
	int			    ind;
	struct 		    hostent *host_name;
	struct 		    sockaddr_in host_address;
    TLSParameters   tls_context;

	if ( argc < 2 ) {
		fprintf( stderr,
				"Usage: %s: [-p http://[username:password@]proxy-host:proxy-port] <URL>\n", 
	    argv[ 0 ] );
		return 1;
	}
	
	/* Proxies are a bit tricky for SSL. A socket had to be created from the client to the server before
     * a document could be requested. This means that the client had to be able to construct a SYN packet,
     * hand that off to a router, which hands it off to another router, and so on until it's received by
     * the server. The server then constructs its own SYN/ACK packet, hands it off, and so on until it's
     * received by the client. However, in corporate intranet environments, packets from outside the corporate
     * domain are not allowed in and vice versa. In effect, there is no route from the client to the server
     * with which it wants to connect.the client establishes a socket connection with the proxy server first,
     * and issues a GET request to it. After the proxy receives the GET request, the proxy examines the request
     * to determine the host name, resolves the IP address, connects to that IP address on behalf of the client,
     * re-issues the GET request, and forwards the response back to the client. This subtly changes the dynamics
     * of HTTP. What's important to notice is that the client establishes a socket with the proxy server, and
     * the GET request now includes the full URL.
	*/
	proxy_host = proxy_user = proxy_password = host = path = NULL;
	ind = 1;
	if ( !strcmp( "-p", argv[ ind ] ) ) {
		if ( !parse_proxy_param( argv[ ++ind ], &proxy_host, &proxy_port,
				&proxy_user, &proxy_password ) ) {
			fprintf( stderr, "Error - malformed proxy parameter '%s'.\n", argv[ 2 ] );
			return 2;
		}
		ind++;
	}
	
	if ( parse_url( argv[ind], &host, &path ) == -1 ) {
		fprintf( stderr, "Error - malformed URL '%s'.\n", argv[ 1 ] );
		return 1;
	}
	
	if ( proxy_host ) {
		printf( "Connecting to host '%s'\n", proxy_host );
		host_name = gethostbyname( proxy_host );
	} 
	else {
		printf( "Connecting to host '%s'\n", host );
		host_name = gethostbyname( host );
	}

    if ( !host_name ) {
        perror( "Error in name resolution" );
        return 3;
    }

	/*
	Open a socket connection on http port with the destination host.
	*/
	client_connection = socket( PF_INET, SOCK_STREAM, 0 );

	if ( !client_connection ) {
		perror( "Unable to create local socket" );
		return 2;
	}

	host_address.sin_family = AF_INET;
	host_address.sin_port = htons( proxy_host ? proxy_port : HTTPS_PORT  );
	memcpy( &host_address.sin_addr, host_name->h_addr_list[ 0 ], sizeof( struct in_addr ) );

	if ( connect( client_connection, ( struct sockaddr * ) &host_address, sizeof( host_address ) ) == -1 ) {
		perror( "Unable to connect to host" );
		return 4;
	}

    printf( "Connection complete; negotiating TLS parameters\n" );

    if ( tls_connect( client_connection, &tls_context ) ) {
        fprintf( stderr, "Error: unable to negotiate TLS connection.\n" );
        return 3;
    }

	printf( "Retrieving document: '%s'\n", path );
	http_get( client_connection, path, host, &tls_context);

	display_result( client_connection, &tls_context);

    tls_shutdown( client_connection, &tls_context );

	printf( "Shutting down.\n" );

	if ( close( client_connection ) == -1 ) {
		perror( "Error closing client connection" );
		return 5;
	}

	return 0;
}
