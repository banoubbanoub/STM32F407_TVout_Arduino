#include <Arduino.h>
#include <Engine3D.h>
#define INITIAL_SHOW_STATE 0

Engine3D engine;



char lastct[256];
uint8_t showstate = INITIAL_SHOW_STATE;
uint8_t showallowadvance = 1;
int framessostate = 0;
int showtemp = 0;


void SetupMatrix()
{
    engine.identity(engine.projectionMatrix);
    engine.identity(engine.modelviewMatrix);

    // Set perspective projection matched to 320x200 viewport
    engine.perspective(
        600,                // Field of view parameter
        250,                // Aspect ratio / scaling
        50,                 // Near clipping plane
        8192,               // Far clipping plane
        engine.projectionMatrix
    );
}

int16_t Height(int x, int y, int l)
{
    return engine.cosFixed((x * x + y * y) + l);
}
void  DrawFrame(  )
{
	char * ctx = &lastct[0];
	int x = 0;
	int y = 0;
	int i;
	int sqsiz = engine.gframe&0x7f;
	int newstate = showstate;
    engine.CNFGPenX = 14;
	engine.CNFGPenY = 20;
	memset( engine.frontframe, 0x00, ((FBW/4)*FBH) );
	int16_t rt[16];
	engine.identity( engine.modelviewMatrix );
	engine.identity( engine.projectionMatrix );
	engine.setColor( 1 );
   


	switch( showstate)
   {
	 case 11:  // State that's not in the normal set.  Just displays boxes.
	 {
		for( i = 0; i < 16; i++ )
		{
			int x = i%4;
			int y = i/4;
			x *= (FBW/4);
			y *= (FBH/4);
			engine.setBGColor( i );
			engine.drawRectangle( x, y, x+(FBW/4)-1, y+(FBH/4)-1);
		}
      }break;
	
	 case 10:
    {
		int i;
		for( i = 0; i < 16; i++ )
		{
			engine.CNFGPenX = 14;
			engine.CNFGPenY = (i+1) * 12;
			engine.setColor( i );
			engine.drawText( "Hello", 3 );
			engine.drawRectangle( 120, (i+1)*12, 180, (i+1)*12+12);
		}

		SetupMatrix();
		engine.rotateEuler( engine.projectionMatrix, -20, 0, 0 );
		engine.rotateEuler( engine.modelviewMatrix, framessostate, 0, 0 );

		for( y = 3; y >= 0; y-- )
		for( x = 0; x < 4; x++ )
		{
			engine.setBGColor( x+y*4 );
			engine.modelviewMatrix[11] = 1000 + engine.sinFixed( (x + y)*40 + framessostate*2 );
			engine.modelviewMatrix[3] = 600*x-850;
			engine.modelviewMatrix[7] = 600*y+800 - 850;
			
            engine.drawGeoSphere();
		}


		if( framessostate > 500 ) newstate = 9;
	
      }break;
	 case 9:
      {
		const char * s = "Direct modulation.\nDMA through the I2S Bus!\nTry it yourself!\n\nhttp://github.com/cnlohr/\nchannel3\n";

		i = strlen( s );
		if( i > framessostate ) i = framessostate;
		memcpy( lastct, s, i );
		lastct[i] = 0;
		engine.drawText( lastct, 3 );
		if( framessostate > 500 ) newstate = 0;
	  }break;

	 case 8:
      {
		int16_t lmatrix[16];
		engine.drawText( "Dynamic 3D Meshes", 3 );
		SetupMatrix();
		engine.rotateEuler( engine.projectionMatrix, -20, 0, 0 );
		engine.rotateEuler( engine.modelviewMatrix, 0, 0, framessostate );

		for( y = -18; y < 18; y++ )
		for( x = -18; x < 18; x++ )
		{
			int o = -framessostate*2;
			int t = Height( x, y, o )* 2 + 2000;
			engine.setBGColor( ((t/100)%15) + 1 );
			int nx = Height( x+1, y, o ) *2 + 2000;
			int ny = Height( x, y+1, o ) * 2 + 2000;
			//printf( "%d\n", t );
			int16_t p0[3] = { x*140, y*140, t };
			int16_t p1[3] = { (x+1)*140, y*140, nx };
			int16_t p2[3] = { x*140, (y+1)*140, ny };
			engine.draw3DSegment(p0, p1);
            engine.draw3DSegment(p0, p2);
		}

		if( framessostate > 400 ) newstate = 10;
      }break;
	
	 case 7:
       {
		int16_t lmatrix[16];
		engine.drawText( "Matrix-based 3D engine.", 3 );
		SetupMatrix();
		engine.rotateEuler( engine.projectionMatrix, -20, 0, 0 );
		engine.rotateEuler( engine.modelviewMatrix, framessostate, 0, 0 );
		int sphereset = (framessostate / 120);
		if( sphereset > 2 ) sphereset = 2;
		if( framessostate > 400 )
		{
			newstate = 8;
		}
		for( y = -sphereset; y <= sphereset; y++ )
		for( x = -sphereset; x <= sphereset; x++ )
		{
			if( y == 2 ) continue;
			engine.modelviewMatrix[11] = 1000 + engine.sinFixed( (x + y)*40 + framessostate*2 );
			engine.modelviewMatrix[3] = 500*x;
			engine.modelviewMatrix[7] = 500*y+800;
			
            engine.drawGeoSphere();
		}

      }break;
	
	 case 6:
	 {
		engine.drawText( "Lines on double-buffered 232x220.", 2 );
		if( framessostate > 60 )
		{
			for( i = 0; i < 350; i++ )
			{
				engine.setColor( rand()%16 );
				engine.line( rand()%FBW2, rand()%(FBH-30)+30, rand()%FBW2, rand()%(FBH-30)+30, rand()%16 );
			}
		}
		if( framessostate > 240 )
		{
			newstate = 7;
		}
      }break;
	 case 5:
	  {
		memcpy(engine.frontframe, (uint8_t*)(framessostate*(FBW/8)+0x3FFF8000), ((FBW/4)*FBH) );
		engine.setBGColor( 17 );
		engine.drawRectangle( 70, 110, 180+200, 150 );		
		engine.setColor( 16 );
		if( framessostate > 160 ) newstate = 6;
	  }break;
	 case 4:
     {
		engine.CNFGPenY += 14*7;
		engine.CNFGPenX += 60;
		engine.drawText( "38x14 TEXT MODE", 2 );

		engine.CNFGPenY += 14;
		engine.CNFGPenX -= 5;
		engine.drawText( "...on 232x220 gfx", 2 );

		if( framessostate > 60 && showstate == 4 )
		{
			newstate = 5;
		}
	 }break;
	 case 3:
	 {
		for( y = 0; y < 14; y++ )
		{
			for( x = 0; x < 38; x++ )
			{
				i = x + y + 1;
				if( i < framessostate && i > framessostate - 60 )
					lastct[x] = ( i!=10 && i!=9 )?i:' ';
				else
					lastct[x] = ' ';
			}
			if( y == 7 )
			{
				memcpy( lastct + 10, "36x12 TEXT MODE", 15 );
			}
			lastct[x] = 0;
			engine.drawText( lastct, 2 );
			engine.CNFGPenY += 14;
			if( framessostate > 120 ) newstate = 4;
		}
	 
	 }break;
	 case 2:
	 {
		ctx += sprintf( ctx, "ESP8266 Features:\n 802.11 Stack\n Xtensa Core @80 or 160 MHz\n 64kB IRAM\n 96kB DRAM\n 16 GPIO\n\
      SPI\n UART\n PWM\n ADC\n I2S with DMA\n                                                   \n Analog Broadcast Television\n" );
		int il = ctx - lastct;
		if( framessostate/2 < il )
			lastct[framessostate/2] = 0;
		else 
			showtemp++;
		engine.drawText( lastct, 2 );
		if( showtemp == 60 ) newstate = 3;
	 }break;
	 case 1:
	 {	i = strlen( lastct );
		lastct[i-framessostate] = 0;
		if( i-framessostate == 1 ) newstate = 2;
      }break;
	
	}

	if( showstate != newstate && showallowadvance )
	{
		showstate = newstate;
		framessostate = 0;
		showtemp = 0;
	}
	else
	{
		framessostate++;
	}

}
