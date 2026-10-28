

void main(void) {

    int i;
    int j;
    int k;
    
    for (i = 1 ; i < 10 ; i++) 
    {
        for (j = 25 ; j < 27; j++) 
        {
            k = j * i;
            printf("Yay it's %d ", k);
        }
        printf("\n That was with i = %d\n", i);

    }
    printf("Done with all iterations\n");
}