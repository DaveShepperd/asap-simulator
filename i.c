typedef struct
{
	int a1;
	short a2;
	char a3;
} Sample_t;

Sample_t *begin(int arg1, short arg2, char arg3)
{
	Sample_t items = { 300, 200, 100 };
	items.a1 += arg1;
	items.a2 += arg2;
	items.a3 += arg3;
	return &items;
}
