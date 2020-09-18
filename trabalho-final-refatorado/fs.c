  /*
  * RSFS - Really Simple File System
  *
  * Copyright © 2010 Gustavo Maciel Dias Vieira
  * Copyright © 2010 Rodrigo Rocco Barbieri
  *
  * This file is part of RSFS.
  *
  * RSFS is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */

/* 
  Projeto 3 - Entrega 1
  Jesimiel Efraim Dias - 726544
*/
  #include <stdio.h>
  #include <string.h>

  #include "disk.h"
  #include "fs.h"

  #define CLUSTERSIZE 4096
  #define OPEN_MAX 2 //Tamanho máximo da estrutura de arquivos abertos. (Só pode ter dois abertos (um read e outro write).
  
  #define SECTOR_TO_SECTORFAT 2048 //Para realizar a conversão de setores do dispositivo para a fat em setores que
								   //serão carregados para a memória principal.

int achar_nome_dir(char *nome);
int achar_posicao_dir();
int achar_agrupamento_fat();
int salvar_fat();
void limparCluster(char *buffer);
int achar_posicao_lista();

unsigned short fat[65536]; 
  
typedef struct {
	char used;
    char name[25];
    unsigned short first_block;
    int size;
} dir_entry;

 dir_entry dir[128];

  /*lista com arquivos*/
typedef struct {
	  int indice_dir;
	  int estado; //fechado, aberto para escrita, ou aberto para leitura (-1, FS_R, FS_W)
	  char cluster[CLUSTERSIZE+1]; //conteudo do arquivo
	  int ultimo_indice_bloco; //aponta para o ultimo byte do bloco que está ocupadp
	  int ultimo_bloco; //pos do bloco em que atualmente estamos escrevendo/lendo
	  int tam;
} Arquivo;


Arquivo lista[OPEN_MAX];//lista de arquivos


  /*variavel que indica se disco esta formatado corretamente: 1 se sim, 0 se não;*/
int formatado = 1;

 
 int fs_init(){
	char * buffer;
	int i,j;
	
	buffer = (char*)fat;
	int setores = bl_size()/SECTOR_TO_SECTORFAT;//Essa é a quantidade real de setores de acordo com o tamanho do dispositivo.
	
	//bl_read recebe qual o setor que ela deve ler, e os dados lidos sao salvos
	//note que a fat possui no máximo 256 setores.
	for( i = 0; i < setores; i++){
		if(!bl_read(i, &buffer[i*SECTORSIZE])){
			printf("Erro: incapaz de ler do dispositivo\n");
			return 0;
		}
	}

	//vamos ler o diretorio:
	buffer = (char *) dir;
	
	//carrega o diretorio do disco para memoria 
	for (i = 0; i < 8; i++){
		if (bl_read (256 + i, &buffer[i * SECTORSIZE]) == 0){
			printf("Erro: incapaz de ler do dispositivo\n");
			//erro de leitura no disco
			return 0;
		}
	}

	//checar integridade do fs:

  //se nao esta corretamente formatado, avisamos o usuario
	for(j=0;j<32;j++){
		if(fat[j] != 3){
			formatado = 0;
			printf("Aviso: dispositivo não está formatado!\n");
			return 1;
		}
	}    
  
	//se diretorio violado:
	if(fat[32] != 4){
		formatado = 0;
			printf("Aviso: dispositivo não está formatado!\n");
		return 1;
   }

	//inicializar a estrutura de arquivos abertos:
	for(i = 0; i < OPEN_MAX; i++){
		lista[i].estado = -1;
	}	

	return 1;
 }

 int fs_format() {
 
    int i;
    int agrupamentos = bl_size()/8; //Quantidade real de agrupamentos da fat de acordo com o dispositivo.
    
    /*colocando os valores corretos na fat em memoria primaria*/
    for(i=0; i < 32; i++) fat[i] = 3;
  
    fat[32] = 4;

    for(i = 33; i < agrupamentos; i++) fat[i] = 1;
    

    /*colocando valores corretos do diretorio na memoria primaria*/
	for (i = 0 ; i < 128 ; i++){
		 dir[i].used = 0;
		 dir[i].name[0] = '\0';
	}
	
	for(i = 0; i < OPEN_MAX; i++) lista[i].estado = -1;
	
    if(salvar_fat() == -1){
		printf("Formatação do disco falhou\n");
		formatado = 0;
		return 0;
    }

    /*se formatacao deu certo, aviso usuario e saio com sucesso */
    printf("Formatação bem sucedida\n");
    formatado = 1;
    return 1;

 }
  
int fs_free() {
  int acumulador = bl_size()* SECTORSIZE;
 

  /*testo pra ver se esta formatado: */
  if(!formatado){
	printf("Erro: dispositivo no formato incorreto\n");
    return 0;
  }
  
  //Soma a quantidade de bytes ocupados por diretórios. 
  for(int i = 0; i < 128; i++) if(dir[i].used == 1) acumulador-=dir[i].size;
  
  //Subtrai o tamanho do dispositivo pelo valor ocupado pelos diretórios retornando o espaço livre.
  return acumulador;
}



int fs_list(char *buffer, int size) {
	int i;
	char aux[100];

	buffer[0] = '\0';
    
    /*testo pra ver se esta formatado: */
    if(!formatado){
      printf("Erro: dispositivo no formato incorreto\n");
      return 0;
    }
	
	//Percorre o diretório colocando as informações em um buffer auxiliar para depois colocar no que irá para
	//saída padrão.
	
	for (i = 0; i < 128; i++){				
		if(dir[i].used == 1){				
			sprintf(aux, "%s\t\t%d\n", dir[i].name, dir[i].size);
			strcat(buffer, aux);
		}
	}

  return 1;
}


int fs_create(char* file_name) {
 
	int dir_livre, fat_livre,i;

	//testo pra ver se esta formatado: * /
	if(!formatado){
		printf("Erro: dispositivo no formato incorreto\n");
		return 0;
	}
	
	//Verifica o tamanho do nome do diretório.
	for (i = 0; i < 25 && file_name[i] != '\0'; i++);
	
	if (i == 25){
		printf("Erro: Nome do arquivo excede limite permitido\n");
		return 0; 
	}
    
    //Verifica se o nome já existe.
	if((dir_livre = achar_nome_dir(file_name)) != -1){
		printf("Erro: arquivo %s já existe\n", file_name);
		return 0;
	}
	
    //Verifica se tem posição livre para o diretório.
	if((dir_livre = achar_posicao_dir()) == -1){
		printf("Erro: o diretório está cheio\n");
		return 0;
	}
	
	//Verifica se a fat possui um agrupamento livre.
	if((fat_livre = achar_agrupamento_fat()) != -1){
		//Ocupa o diretório livre.
		dir[dir_livre].used = 1;
		strcpy(dir[dir_livre].name, file_name);
		dir[dir_livre].size = 0;
		//Ocupa o agrupamento livre.
		dir[dir_livre].first_block = fat_livre;
		fat[fat_livre] = 2;
			
		//Salva a fat.
		if(salvar_fat() == -1) {
			return 0;
		} 
		
		return 1;
	}
	return 0;
}


int fs_remove(char *file_name) {
    int i;
    /*testo pra ver se esta formatado: */ 
	if(!formatado){
      printf("Erro: dispositivo no formato incorreto\n");
    }

    //Verificamos o nome do diretório.
	if((i = achar_nome_dir(file_name)) != -1){
		
		//Liberamos todos os agrupamentos do diretório.
		int temp;
	
		while(fat[dir[i].first_block] != 2){
			temp = fat[dir[i].first_block];
			fat[dir[i].first_block] = 1;
			dir[i].first_block = temp;
		}
		fat[dir[i].first_block] = 1;
		
		//Liberamos o diretório.
		dir[i].used = 0;
		dir[i].name[0] = '\0';
		
						
		//Salvamos a fat.
		if(salvar_fat() == -1) {
			return 0;
		} 
        
		return 1;
		
	}
	printf("Arquivo %s não encontrado\n", file_name);
	return 0;
}


int fs_open(char *file_name, int mode) {
	int i,livre;
	  /*testo pra ver se esta formatado: */ 
	if(!formatado){
		printf("Erro: dispositivo no formato incorreto\n");
	}
	
	if(mode == -1){
		printf("Erro: modo inválido.");
		return -1;
	}

	
	i = achar_nome_dir(file_name);//Achamos o nome do diretório.
	
	//Achamos uma posição livre na estrutura de diretórios abertos.
	if((livre = achar_posicao_lista()) == -1) return 0;
	
	//Verificamos se o diretório existe.
	if (mode == FS_R && i != -1) { 
		//Passamos as informações necessárias do diretório para estrutura de arquivos abertos.
		lista[livre].indice_dir = i;
		lista[livre].estado = FS_R;
		lista[livre].ultimo_bloco = dir[i].first_block; // bloco atual "aponta" pro primeiro bloco
		limparCluster(lista[livre].cluster);
		lista[livre].ultimo_indice_bloco = CLUSTERSIZE; //Inicia com tamanho do cluster para no fs_read carregar
														//um cluster pela primeira vez.
		lista[livre].tam = 0;
	}
  
	else if (mode == FS_W) { // Verificando se foi aberto para escrita.
		if (i != -1) { // Verificamos se diretório já existe.
			if(!fs_remove(file_name)) { // verifica erro ao apagar diretório existente
					printf("Erro ao apagar arquivo já existente\n");
					return -1; // erro
			}
		}
		if (!fs_create(file_name)) { // verifica erro ao criar o diretório.
			printf("Erro ao criar arquivo\n");
			return -1; // erro
		 }
		
		  //Procuramos o diretório.
		 if((i = achar_nome_dir(file_name)) != -1){ // compara os nomes dos diretórios.
			lista[livre].indice_dir = i; //Indice do diretório.
			lista[livre].estado = FS_W; // estado = 1
			lista[livre].ultimo_bloco = dir[i].first_block; // bloco atual "aponta" pro primeiro bloco
			limparCluster(lista[livre].cluster);
			lista[livre].ultimo_indice_bloco = 0;
		 }
	}
 
 
	return livre; // posição do diretório na estrutura de arquivos abertos.
  
}

int fs_close(int file){
	
	  /*testo pra ver se esta formatado: */ 
	if(!formatado){
		printf("Erro: dispositivo no formato incorreto\n");
	}
	
	
	if(lista[file].estado != -1){
		if(lista[file].estado == FS_W){
			for(int i = 0; lista[file].cluster[i*SECTORSIZE] != '\0' && i < 8; i++){
				if(bl_write(lista[file].ultimo_bloco*8 + i, &lista[file].cluster[i*SECTORSIZE]) == 0){
					printf("Erro na escrita do arquivo\n");
					return -1;
				}
			}		
			if(salvar_fat() == -1) return 0;
		}	
		
		lista[file].estado = -1;

		return 1;
	}
	printf("O arquivo nao esta aberto\n");
	return 0;
}


int fs_write(char *buffer, int size, int file) {
	int i, j;

	if(!formatado){
    printf("Erro: dispositivo no formato incorreto\n");
		return -1;
	}

	if(size == 0){
		return 0;
	}

	if(lista[file].estado != FS_W){
		printf("Erro: arquivo nao esta aberto para escrita. \n");
		return -1;
	}
	
	dir[lista[file].indice_dir].size += size;

		
	//Enchamos o cluster com os bytes até o buffer estar esgotado ou não caber mais.
	for(i = 0; lista[file].ultimo_indice_bloco < CLUSTERSIZE; i++,lista[file].ultimo_indice_bloco++){
		if(i == size) break;
		lista[file].cluster[lista[file].ultimo_indice_bloco] = buffer[i];
	}
	
	//Verificamos se o cluster não está cheio.
	if(lista[file].ultimo_indice_bloco == CLUSTERSIZE) {
		//Descarregamos o cluster na memória não volátil.
		for(j = 0 ; lista[file].cluster[j*SECTORSIZE] != '\0' && j < 8 ; j++){   //Descarregando o cluster no disco
			if(!bl_write(lista[file].ultimo_bloco*8 + j, &lista[file].cluster[j*SECTORSIZE])){
				printf("Não foi possível efetuar a escrita");
				return -1;
			}
		}
		
		//Limpamos o cluster.
		limparCluster(lista[file].cluster);
			
		//Caso o i menor do que o size ainda sobraram bytes no buffer.
		if(i < size){
			lista[file].ultimo_indice_bloco = 0; //O indice do cluster é resetado.
				
			//Enchemos o novo cluster com os bytes do buffer que restaram.
			for(; i < size; i++,lista[file].ultimo_indice_bloco++){
				lista[file].cluster[lista[file].ultimo_indice_bloco] = buffer[i];
			}
				
			//Por causa do novo cluster precisamos de um novo agrupamento.
			//salva o agrumento atual na variável auxiliar.
			int aux = lista[file].ultimo_bloco;
					
			//Tentamos obter um novo agrupamento caso tenha espaço no dispositivo.
			if((lista[file].ultimo_bloco = achar_agrupamento_fat()) == -1){
				return -1;
			}
			//Caso tenha um novo agrupamento disponível o atual que antes indicava o final do arquivo
			//agora aponta para o próximo.
			fat[aux] = lista[file].ultimo_bloco;
				
			//O novo agrupamento agora marca o fim do arquivo.
			fat[lista[file].ultimo_bloco] = 2;
		}	
			
			//Salvamos na memória não volátil.
		if(salvar_fat() == -1) return -1;
	}
	
	//Limpamos o buffer.
	buffer[0] = '\0';
	return size;
}


int fs_read(char *buffer, int size, int file){
	int i, j;
	
	if(lista[file].estado == FS_R){
		buffer[0] = '\0';//Limpamos o buffer.
		
		for(i = 0; i < size; i++){
			
			//Carregará um novo cluster quando o atual estiver cheio ou quando for o primeiro cluster a ser carregado.
			if (lista[file].ultimo_indice_bloco == CLUSTERSIZE){  
				//lista[file].cluster[0] = '\0';//Limpa o buffer antes de carregar um novo cluster.
				for(j = 0 ; j < 8 ; j++){   
					if(!bl_read(lista[file].ultimo_bloco*8+j, &lista[file].cluster[j*SECTORSIZE])){
						return -1;
					}
				}
				//Após carregado o cluster atualizamos o bloco.
				lista[file].ultimo_bloco = fat[lista[file].ultimo_bloco];
				//E o indice que indica em que byte do cluster estamos passando para o buffer é reinicializado.   
				lista[file].ultimo_indice_bloco = 0;
			}
			//Caso seja o último byte do arquivo damos break pois já chegamos no final do mesmo.
			if(lista[file].tam == dir[lista[file].indice_dir].size){
				break;
			}
			
			buffer[i] = lista[file].cluster[lista[file].ultimo_indice_bloco];//Passando byte a byte do cluster para o buffer.
			lista[file].ultimo_indice_bloco++;//Indica em que bytes do cluster estamos.
			lista[file].tam++; //Indica em que byte do arquivo estamos.
			
		}
		return i; //Retornamos a quantidade de bytes escritos no buffer.
	}
	printf("O arquivo não está aberto\n");
	return -1;
}

//Retorna o agrupamento livre da fat caso a mesma o tenha.
int achar_agrupamento_fat(){
	
	//Tamanho real do dispositivo.
	int agrupamento = bl_size()/8;

    for(int i = 33; i < agrupamento; i++){
        if(fat[i] == 1){//Agrupamento livre é aquele que tem valor 1.
            return i;
        }
    }

    printf("Nao há mais espaço disponível em disco \n");

    return -1;
}


//Procuramos uma posição livre no diretório.
int achar_posicao_dir(){
		for(int i = 0; i < 128; i++){
			//Caso o diretório não esteja ocupado a variável used é zero.
			if(dir[i].used == 0) return i;
		}
		return -1;
}

//Procuramos um diretório pelo nome e retornamos se existe.
int achar_nome_dir(char *nome){
		
		for(int i = 0; i < 128; i++){
			if(strcmp(nome,dir[i].name) == 0){
					return i;	
			}
		}
		return -1;
}



//Salvamos a fat na memória não volátil.
int salvar_fat(){
	//Quantidade real de setores do dispositivo.
	int setores = bl_size()/SECTOR_TO_SECTORFAT, i;
	char *buffer  = (char*)fat;
		
	for(i = 0 ; i < setores ; i++){
		if(!bl_write(i, &buffer[i*SECTORSIZE])){
			printf("Nao foi possivel escrever FAT no disco!\n");
			return -1;
		}
	}

	buffer = (char *) dir;

	for (i = 0; i < 8; i++) {
			if (bl_write (256+i, &buffer[i * SECTORSIZE]) == 0){
				return -1;
			}
	}
	return 1;
}

//Limpamos o cluster.
void limparCluster(char *buffer){
	//Marcamos cada setor do cluster como vazio.
	//A marcação será essencial para sabermos quantos setores iremos gravar no último cluster.
	for(int i = 0; i < CLUSTERSIZE; i++){
			buffer[i] = '\0';
	}
	
	/*for(int i = 0 ; i < 8 ; i++){ 
		buffer[i*SECTORSIZE] = '\0';
	}*/
}

//Achamos uma posição livre na lista de arquivo abertos.
int achar_posicao_lista(){
	for(int i = 0; i < OPEN_MAX; i++){
			if(lista[i].estado == -1) return i;
	}
	return -1;
}

