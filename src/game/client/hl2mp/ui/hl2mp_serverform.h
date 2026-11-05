#ifndef HL2MP_SERVERFORM_H
#define HL2MP_SERVERFORM_H

#include "hl2mp_menucontrols.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/TextEntry.h"

// Shared, scrollable form cards for hosting and live server administration.
class CHL2MPServerForm : public vgui::EditablePanel
{
    DECLARE_CLASS_SIMPLE(CHL2MPServerForm, vgui::EditablePanel);
    class Row : public vgui::EditablePanel
    {
        DECLARE_CLASS_SIMPLE(Row, vgui::EditablePanel);
    public:
        Row(vgui::Panel *parent, vgui::Label *caption, vgui::Panel *first, vgui::Panel *second, bool note)
            : BaseClass(parent,"FormRow"), m_Caption(caption), m_First(first), m_Second(second), m_Note(note), m_Scale(1) {
            if(caption)caption->SetParent(this); first->SetParent(this); if(second)second->SetParent(this);
            SetPaintBackgroundEnabled(false);
        }
        void Style(vgui::HFont font, vgui::HFont small, float scale) {
            m_Scale=scale;
            vgui::Panel *controls[]={m_Caption,m_First,m_Second};
            for(int i=0;i<ARRAYSIZE(controls);++i) {
                if(!controls[i])continue;
                vgui::Label *label=dynamic_cast<vgui::Label *>(controls[i]);
                if(label) { label->SetFont(m_Note?small:font);label->SetWrap(true);label->SetFgColor(m_Note?HL2MPServerTheme::Muted():HL2MPServerTheme::Text()); }
                CHL2MPDropdown *drop=dynamic_cast<CHL2MPDropdown *>(controls[i]); if(drop)drop->Style(font,scale);
                vgui::TextEntry *entry=dynamic_cast<vgui::TextEntry *>(controls[i]);
                if(entry) { entry->SetFont(font);entry->SetBgColor(Color(29,29,29,255));entry->SetFgColor(HL2MPServerTheme::Text());entry->SetBorder(NULL); }
            }
            InvalidateLayout(true);
        }
        int Measure(vgui::Panel *panel,int width,int minimum) {
            vgui::Label *label=dynamic_cast<vgui::Label *>(panel); if(!label)return minimum;
            const int inset=dynamic_cast<vgui::CheckButton *>(panel)?clamp(vgui::surface()->GetFontTall(label->GetFont()),18,30)+10:12;
            vgui::TextImage *text=label->GetTextImage();text->SetSize(MAX(1,width-inset),1024);text->RecalculateNewLinePositions();
            int tw,th;text->GetContentSize(tw,th);text->SetSize(MAX(1,width-inset),th);
            return MAX(minimum,th+(panel==m_Caption?4:MAX(8,int(10*m_Scale))));
        }
        void PerformLayout() OVERRIDE {
            BaseClass::PerformLayout(); const int gap=MAX(6,int(8*m_Scale)),w=GetWide();int y=0;
            if(m_Caption) { int h=Measure(m_Caption,w,0);m_Caption->SetBounds(0,0,w,h);y=h; }
            const int width=m_Second?(w-gap)/2:w;
            int height=Measure(m_First,width,m_Note?0:MAX(32,int(40*m_Scale)));
            if(m_Second)height=MAX(height,Measure(m_Second,width,MAX(32,int(40*m_Scale))));
            m_First->SetBounds(0,y,width,height);m_First->InvalidateLayout(true);
            if(m_Second) { m_Second->SetBounds(width+gap,y,w-width-gap,height);m_Second->InvalidateLayout(true); }
            SetTall(y+height+gap);
        }
    private:
        vgui::Label *m_Caption;vgui::Panel *m_First,*m_Second;bool m_Note;float m_Scale;
    };
public:
    CHL2MPServerForm(vgui::Panel *parent,const char *title) : BaseClass(parent,"ServerForm"),m_Scale(1),m_Font(0),m_Small(0) {
        m_Title=new vgui::Label(this,"Title",title);m_Title->SetWrap(true);
        m_List=new vgui::PanelListPanel(this,"Fields");m_List->SetFirstColumnWidth(0);m_List->SetVerticalBufferPixels(0);
        m_List->SetPaintBackgroundEnabled(false);m_List->SetBorder(NULL);HL2MPStyleScrollBar(m_List->GetScrollbar());
    }
    void Add(vgui::Panel *control,vgui::Label *caption=NULL,vgui::Panel *second=NULL,bool note=false) {
        Row *row=new Row(this,caption,control,second,note);m_Rows.AddToTail(row);m_List->AddItem(NULL,row);
    }
    void Style(vgui::HFont font,vgui::HFont small,float scale) { m_Font=font;m_Small=small;m_Scale=scale;InvalidateLayout(true); }
    void Refresh() { InvalidateLayout(true); }
    void PerformLayout() OVERRIDE {
        BaseClass::PerformLayout();if(!m_Font)return;
        const int pad=MAX(10,int(16*m_Scale)),width=GetWide()-2*pad;
        m_Title->SetFont(m_Small);m_Title->SetFgColor(HL2MPServerTheme::Orange());
        m_Title->GetTextImage()->SetSize(width,1024);m_Title->GetTextImage()->RecalculateNewLinePositions();
        int tw,th;m_Title->GetTextImage()->GetContentSize(tw,th);
        m_Title->SetBounds(pad,pad,width,th+4);m_Title->InvalidateLayout(true);
        m_List->SetBounds(pad,pad+th+12,width,MAX(1,GetTall()-2*pad-th-12));
        m_List->SetBorder(NULL);m_List->SetPaintBackgroundEnabled(false);
        m_List->GetScrollbar()->SetWide(MAX(12,int(12*m_Scale)));
        FOR_EACH_VEC(m_Rows,i) { m_Rows[i]->SetWide(MAX(1,width-m_List->GetScrollbar()->GetWide()-12));m_Rows[i]->Style(m_Font,m_Small,m_Scale); }
        m_List->InvalidateLayout(true);
    }
    void PaintBackground() OVERRIDE { HL2MPServerTheme::Rounded(0,0,GetWide(),GetTall(),Color(24,24,24,235),MAX(3,int(6*m_Scale))); }
private:
    vgui::Label *m_Title;vgui::PanelListPanel *m_List;CUtlVector<Row *> m_Rows;float m_Scale;vgui::HFont m_Font,m_Small;
};
#endif
