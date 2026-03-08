int input_door=14;
int output_door=27;
int count_ss=35;
int max_ss=32;
int reset_sw=15;
int red_led=33;
int yellow_led=26;
int green_led=25;
//////////////////////////
void setup() 
{
  pinMode(red_led,OUTPUT);
  pinMode(yellow_led,OUTPUT);
  pinMode(green_led,OUTPUT);
  pinMode(input_door,INPUT_PULLUP);
  pinMode(output_door,INPUT_PULLUP);
  pinMode(count_ss,INPUT_PULLUP);
  pinMode(max_ss,INPUT_PULLUP);
  red_off();
  yellow_off();
  green_off();
  red_on();
  delay(500);
  red_off();
  yellow_on();
  delay(500);
  yellow_off();
  green_on();
  delay(500);
  green_off();
  delay(500);
}
////////////////////////
void loop() 
{  
  green_on();
  red_off();
  while(digitalRead(max_ss)==0)
  {
    red_on();
    green_off();
  }
  if(digitalRead(count_ss)==0)
  {
    while(digitalRead(count_ss)==0);
    delay(5);
    yellow_on(); 
  } 
  if(digitalRead(reset_sw)==0)
  {
    yellow_off();
    while(digitalRead(reset_sw)==0);
    delay(20);             
  }
  /*while(digitalRead(input_door)==0||digitalRead(output_door)==0)
  {
    red_on();
    delay(100);
    red_off();
    delay(100);
  }*/
}
///////////////////////
void red_on()
{
  digitalWrite(red_led,1);
}
///////////////////////
void red_off()
{
  digitalWrite(red_led,0);
}
///////////////////////
void yellow_on()
{
  digitalWrite(yellow_led,1);
}
///////////////////////
void yellow_off()
{
  digitalWrite(yellow_led,0);
}
///////////////////////
void green_on()
{
  digitalWrite(green_led,1);
}
///////////////////////
void green_off()
{
  digitalWrite(green_led,0);
}
///////////////////////
