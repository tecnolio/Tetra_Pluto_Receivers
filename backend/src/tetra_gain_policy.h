/* An idle AD9361 gain is a hardware default, not a saved TETRA preference. */
static int tetra_gain_file(const char*path,int fallback){
 int value;FILE*f=fopen(path,"r");if(!f)return fallback;
 int ok=fscanf(f,"%d",&value)==1;fclose(f);
 return ok&&value>=0&&value<=73?value:fallback;
}
static int tetra_start_gain(int receiving,const char*profile,const char*hardware){
 int saved=tetra_gain_file(profile,35);
 return receiving?tetra_gain_file(hardware,saved):saved;
}
