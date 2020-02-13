#include<sys/types.h>
#include<sys/socket.h>
#include<stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1000
#define RESPONSE_SIZE 80

void traitement(int codeHtml, char* fichier, int confd, int pagefd);
int parseRequest(char* requestFromClient, int requestSize, char* string, int stringSize);
char* getContentType(char* fichier);
char *get_filename_ext(const char *filename);
void gestionClient(int confd);

char *get_filename_ext(const char *filename) {
    char *dot = strrchr(filename, '.');
    if(!dot || dot == filename) return "";
    return dot + 1;
}

char* getHtmlHeader(int codeHtml){
	switch (codeHtml)
	{
	case 200:
		return "HTTP/1.1 200 OK\r\n";
	case 400:
		return "HTTP/1.1 400 Bad Request\r\n";
	case 404:
		return "HTTP/1.1 404 Not Found\r\n";
	case 500:
		return "HTTP/1.1 500 Internal Server Error";
	default:
		return "HTTP/1.1 520 Unknown Error";
	}
}

char* getContentType(char* fichier){
	char* fileType = get_filename_ext(fichier);

	if (!strcmp(fileType, "html"))
	{
		return "Content-Type: text/html; charset=UTF-8\r\n\r\n";
	} else if(!strcmp(fileType, "png")){
		return "Content-Type: image/png;\r\n\r\n";
	} else if(!strcmp(fileType, "ico")){
		return "Content-Type: image/x-icon;\r\n\r\n";
	} else if(!strcmp(fileType, "jpg")){
		return "Content-Type: image/jpeg;\r\n\r\n";
	}
	return "Content-Type: text/plain; charset=UTF-8\r\n\r\n";
}

void gestionClient(int confd){
	char buffer[BUFFER_SIZE];
	char parseResult[BUFFER_SIZE];
	char lien[BUFFER_SIZE] = "public/";

	if(recv(confd, buffer, BUFFER_SIZE, 0)<0) perror ("Erreur recv");

	if(parseRequest(buffer,BUFFER_SIZE,parseResult, BUFFER_SIZE)){
		traitement(400,strcat(lien, "file400.html"), confd, 0);
	}
	else{
		int pagefd = open(strcat(lien,parseResult), O_RDONLY);
		if(pagefd < 0){
			if(errno == ENOENT){
				traitement(404,"public/file404.html",confd, 0);
			}
			else if (errno == EACCES){
				traitement(500,"public/file500.html",confd, 0);
			}
			close(pagefd);
		}
		else{
			traitement(200,"public/index.html", confd, pagefd);
		}
	}
	fprintf(stderr,"LOG: requested link:  %s\n",parseResult);
}

void traitement(int codeHtml, char* fichier, int confd, int pagefd){
	char message[BUFFER_SIZE] = "";
	char page[80];
	int nread;

	//En-tête HTML
	strcat(message, getHtmlHeader(codeHtml));
	strcat(message, getContentType(fichier));

	if(send(confd, message, strlen(message), 0) != (ssize_t) strlen(message)) perror ("Erreur send");

	if(codeHtml != 200){
		pagefd = open(fichier, O_RDONLY);
	}

	while((nread = read(pagefd, page, RESPONSE_SIZE)) > 0){
		if((send(confd,page,nread, 0) != nread)) perror("erreur snd");
	}

	close(pagefd);
}

int main(int argc, char** argv){
	(void)(argc);
	(void)(argv);
    struct addrinfo hints, *res;
    int sockfd, confd;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if(getaddrinfo(NULL,"4000",&hints,&res)!=0) perror("Erreur getaddrinfo()");
    if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol))<0) perror("Erreur socket():"); 

    if(bind(sockfd, res->ai_addr , res->ai_addrlen) < 0) perror("Erreur bind()");
    if(listen(sockfd, 60)<0) perror("Erreur listen()");

	for(;;){
		if ((confd=accept(sockfd, res->ai_addr, &res->ai_addrlen)) > 0){
			pid_t pid = fork();
			if (pid == 0){

				struct sockaddr_in6* client = (struct sockaddr_in6*) res->ai_addr;
				struct in6_addr ipAddr = client->sin6_addr;
				char str[INET6_ADDRSTRLEN];
				inet_ntop( AF_INET6, &ipAddr, str, INET6_ADDRSTRLEN);
				
				fprintf(stderr,"LOG: ip:  %s\n",str);
				gestionClient(confd);
				return 0;
			}
			close(confd);
			waitpid(pid, NULL, WNOHANG);
		}
		else perror ("Erreur accept");
	}
    freeaddrinfo(res);
    return 0;
}

/* Lit la requete du client. Met le nom du fichier demande dans string.
* Si la syntaxe est incorrecte ou si il manque des retour charriots
* on renvoi -1. Autrement la fonction renvoie 0.
* requestFromClient est la chaine de 1000 octets censee contenir la requete provenant du client.
* requestSize doit etre egale a 1000 (et pas a la taille de la chaine de caractere). 
*/

int parseRequest(char* requestFromClient, int requestSize, char* string, int stringSize)
{
	/* charPtr[4] est un tableau de 4 pointeurs pointant sur le debut de la chaine, les 2 espaces 	*/
	/* de la requete (celui	apres le GET et celui apres le nom de fichier) et sur le premier '\r'.	*/
	/* Le pointeur end sera utilise pour mettre un '\0' a la fin du doubl retour charriot.		*/

	char *charPtr[4], *end;

	/* On cherche le double retour charriot	dans requestFromClient
	* suivant les systemes, on utilise \r ou \n (new line, new feed)
	* par convention en http on utilise les deux \r\n mais cela represente en pratique un seul retour charriot.
	* Pour simplifier ici, on ne recherche que les '\n'.
	* On placera un '\0' juste apres le double retour charriot permettant de traiter la requete 
	* comme une chaine de caractere et d'utiliser les fcts de la bibliotheque string.h. 
	*/

	/* Lecture jusqu'au double retour charriot	*/
	requestFromClient[requestSize-1]='\0';//Permet d'utiliser strchr() - attention ne marche pas si requestSize indique la taille de la chaine de caractere

	if( (end=strstr(requestFromClient,"\r\n\r\n"))==NULL) return(-1);
	*(end+4)='\0';
	
	// Verification de la syntaxe (GET fichier HTTP/1.1) 		
	charPtr[0]=requestFromClient;	//Debut de la requete (GET en principe)
	//On cherche le premier espace, code ascii en 0x20 (en hexa), c'est le debut du nom du fichier
	charPtr[1]=strchr(requestFromClient,' ');	
	if(charPtr[1]==NULL) return(-1);
	charPtr[2]=strchr(charPtr[1]+1,' ');	
	if(charPtr[2]==NULL) return(-1);
	charPtr[3]=strchr(charPtr[2]+1,'\r');	
	if(charPtr[3]==NULL) return(-1);

	//On separe les chaines
	*charPtr[1]='\0';
	*charPtr[2]='\0';
	*charPtr[3]='\0';

	if(strcmp(charPtr[0],"GET")!=0) return(-1);
	if(strcmp(charPtr[2]+1,"HTTP/1.1")!=0) return(-1);
	strncpy(string,charPtr[1]+2,stringSize);//On decale la chaine de 2 octets: le premier octet est le '\0', le deuxieme decalage permet de retirer le "/" 

	//Si stringSize n'est pas suffisement grand, la chaine ne contient pas de '\0'. Pour verifier il suffit de tester string[stringSize-1] qui
	// doit etre = '\0' car strncpy remplit la chaine avec des '\0' quand il y a de la place.
	if(string[stringSize-1]!='\0'){
		fprintf(stderr,"Erreur parseRequest(): la taille de la chaine string n'est pas suffisante (stringSize=%d)\n",stringSize);
		exit(3);
	}
	
	//DEBUG - Vous pouvez le supprimer si vous le souhaitez.
	if( *(charPtr[1]+2) == '\0') fprintf(stderr,"DEBUG-SERVEUR: le nom de fichier demande est vide -\nDEBUG-SERVEUR: - on associe donc le fichier par defaut index.html\n");
	else fprintf(stderr,"DEBUG-SERVEUR: le nom de fichier demande est %s\n",string);

	if( *(charPtr[1]+2) == '\0') strcpy(string,"index.html");

	return(0);
}