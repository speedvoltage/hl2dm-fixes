#ifndef HL2MP_COLORPICKER_H
#define HL2MP_COLORPICKER_H
#include "hl2mp_menucontrols.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/TextEntry.h"

// Embedded picker: no popup/modal capture, so previews stay visible while editing.
class CHL2MPColorPicker : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE(CHL2MPColorPicker,vgui::EditablePanel);
public:
    CHL2MPColorPicker(vgui::Panel *parent, const char *name) : BaseClass(parent,name), m_Color(255,0,0,255), m_Drag(0), m_Writing(false)
    {
        SetMouseInputEnabled(true); SetKeyBoardInputEnabled(true);
        m_Hex = new vgui::TextEntry(this,"HexColor"); m_Hex->SetMaximumCharCount(7); m_Hex->AddActionSignalTarget(this);
        SetColor(m_Color);
    }
    Color GetColor() const { return m_Color; }
    void SetColor(Color color)
    {
        m_Color=color; Vector rgb(color.r()/255.f,color.g()/255.f,color.b()/255.f); RGBtoHSV(rgb,m_HSV); UpdateHex();
    }
    void SetRGB(const char *text)
    {
        int r,g,b; if(sscanf(text,"%d %d %d",&r,&g,&b)==3) SetColor(Color(clamp(r,0,255),clamp(g,0,255),clamp(b,0,255),255));
    }
    CUtlString Value() const { return CUtlString(VarArgs("%d %d %d",m_Color.r(),m_Color.g(),m_Color.b())); }
    void Style(vgui::HFont font) { m_Hex->SetFont(font); m_Hex->SetBgColor(Color(38,40,42,255)); m_Hex->SetFgColor(HL2MPServerTheme::Text()); }
    void PerformLayout() OVERRIDE
    {
        BaseClass::PerformLayout(); m_FieldW=MAX(32,GetWide()-38); m_FieldH=MAX(32,GetTall()-72);
        m_Hex->SetBounds(0,GetTall()-32,GetWide(),30);
    }
    void PaintBackground() OVERRIDE
    {
        for(int y=0;y<24;++y) for(int x=0;x<32;++x)
            Rect(x*m_FieldW/32,y*m_FieldH/24,(x+1)*m_FieldW/32,(y+1)*m_FieldH/24,RGB(Vector(m_HSV.x,x/31.f,1-y/23.f)));
        for(int y=0;y<m_FieldH;++y) Rect(m_FieldW+12,y,GetWide(),y+1,RGB(Vector(y*360.f/m_FieldH,1,1)));
        int x=int(m_HSV.y*(m_FieldW-1)), y=int((1-m_HSV.z)*(m_FieldH-1));
        vgui::surface()->DrawSetColor(Color(0,0,0,255)); vgui::surface()->DrawOutlinedCircle(x,y,5,16);
        vgui::surface()->DrawSetColor(Color(255,255,255,255)); vgui::surface()->DrawOutlinedCircle(x,y,4,16);
        y=int(m_HSV.x/360*(m_FieldH-1)); Rect(m_FieldW+8,y,GetWide(),y+2,Color(255,255,255,255));
        for(int i=0;i<12;++i) Rect(i*GetWide()/12,m_FieldH+10,(i+1)*GetWide()/12-3,m_FieldH+32,Swatch(i));
    }
    void OnMousePressed(vgui::MouseCode code) OVERRIDE
    {
        if(code!=MOUSE_LEFT || !IsEnabled())return;
        int x,y; vgui::input()->GetCursorPos(x,y); ScreenToLocal(x,y);
        if(y>=m_FieldH+10 && y<m_FieldH+32) { SetColor(Swatch(clamp(x*12/MAX(1,GetWide()),0,11))); return; }
        m_Drag = y>=0 && y<m_FieldH ? (x<m_FieldW ? 1 : x>=m_FieldW+12 ? 2 : 0) : 0;
        if(m_Drag) { vgui::input()->SetMouseCapture(GetVPanel()); OnCursorMoved(x,y); }
    }
    void OnCursorMoved(int x,int y) OVERRIDE
    {
        if(!m_Drag)return;
        if(m_Drag==1) { m_HSV.y=clamp(float(x)/MAX(1,m_FieldW-1),0.f,1.f); m_HSV.z=1-clamp(float(y)/MAX(1,m_FieldH-1),0.f,1.f); }
        else m_HSV.x=clamp(float(y)/MAX(1,m_FieldH-1),0.f,.9999f)*360;
        m_Color=RGB(m_HSV); UpdateHex();
    }
    void OnMouseReleased(vgui::MouseCode) OVERRIDE { m_Drag=0; if(vgui::input()->GetMouseCapture()==GetVPanel())vgui::input()->SetMouseCapture(NULL); }
    void OnMouseCaptureLost() OVERRIDE { m_Drag=0; }
private:
    MESSAGE_FUNC(HexChanged,"TextChanged")
    {
        if(m_Writing)return; char text[16];m_Hex->GetText(text,sizeof(text));const char *p=text[0]=='#'?text+1:text;
        if(Q_strlen(p)!=6)return; int value=0;
        for(int i=0;i<6;++i) { char c=p[i];int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(n<0)return;value=value*16+n; }
        Color color(value>>16,(value>>8)&255,value&255,255);
        if (color != m_Color) SetColor(color);
    }
    void UpdateHex() { m_Writing=true;m_Hex->SetText(VarArgs("#%02X%02X%02X",m_Color.r(),m_Color.g(),m_Color.b()));m_Writing=false;Repaint(); }
    static Color RGB(const Vector &hsv) { Vector rgb;HSVtoRGB(hsv,rgb);return Color(int(rgb.x*255+.5f),int(rgb.y*255+.5f),int(rgb.z*255+.5f),255); }
    static Color Swatch(int i)
    {
        const Color colors[]={Color(255,255,255,255),Color(255,183,67,255),Color(255,0,0,255),Color(255,128,0,255),Color(255,230,0,255),Color(0,255,0,255),Color(0,220,180,255),Color(50,150,255,255),Color(80,80,255,255),Color(180,80,255,255),Color(255,100,180,255),Color(0,0,0,255)};return colors[i];
    }
    static void Rect(int x,int y,int right,int bottom,Color color) { vgui::surface()->DrawSetColor(color);vgui::surface()->DrawFilledRect(x,y,right,bottom); }
    Color m_Color;Vector m_HSV;int m_FieldW=32,m_FieldH=32,m_Drag;bool m_Writing;vgui::TextEntry *m_Hex;
};
#endif
