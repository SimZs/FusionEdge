
#include "../../core/options.h"
#if DSP_MODEL!=DSP_DUMMY
#include "../display_select.h"
#include "pages.h"
#include "widgets.h"

void Pager::begin(){

}

void Pager::loop(){
  for(const auto& p: _pages)
    if(p->isActive()) p->loop();
}

Page& Pager::addPage(Page* page, bool setNow){
  _pages.push_back(page);
  if(setNow) setPage(page);
  return *page;
}

bool Pager::removePage(Page* page){
  page->setActive(false);
  dsp.clearDsp();
 // dsp.fillScreen(config.theme.background);  // vagy TFT_BLACK
  auto i = std::find_if(_pages.begin(), _pages.end(), [&page](const Page* pn){ return page == pn; });
  if (i != _pages.end()){
    delete (*i);
    (*i) = nullptr;
    _pages.erase(i);
    return true;
  }
  return false;
}

void Pager::setPage(Page* page, bool black){
  for(const auto& p: _pages) p->setActive(false);
  dsp.clearDsp(black);
  page->setActive(true);
}



/*******************************************************/
Page::~Page() {
  // Do not erase from a std::list while a range-for iterator points at the
  // erased node. Boot pages are destroyed at startup, so the old loop was a
  // real use-after-free on every normal boot.
  while (!_widgets.empty()) {
    Widget* widget = _widgets.front();
    _widgets.pop_front();
    delete widget;
  }
  // Child pages are shared (for example the footer), therefore non-owning.
  _pages.clear();
}

void Page::loop() {
  if(_active) for (const auto& w : _widgets) w->loop();
}

Widget& Page::addWidget(Widget* widget) {
  _widgets.push_back(widget);
  widget->setActive(_active, _active);
  return *widget;
}

bool Page::removeWidget(Widget* widget){
  if (!widget) return false;
  auto i = std::find_if(_widgets.begin(), _widgets.end(), [&widget](const Widget* wn){ return widget == wn; });
  if (i != _widgets.end()){
    Widget* removed = *i;
    removed->setActive(false, _active);
    _widgets.erase(i);
    delete removed;
    return true;
  }
  return false;

  //return _widgets.remove(widget);
}

Page& Page::addPage(Page* page){
  _pages.push_back(page);
  return *page;
}

bool Page::removePage(Page* page){
  if (!page) return false;
  auto i = std::find_if(_pages.begin(), _pages.end(), [&page](const Page* pn){ return page == pn; });
  if (i != _pages.end()){
    Page* removed = *i;
    _pages.erase(i);
    delete removed;
    return true;
  }
  return false;
//  return _pages.remove(page);
}

void Page::setActive(bool act) {
  _active = act;
  for(const auto& w: _widgets) w->setActive(act);
  for(const auto& p: _pages) p->setActive(act);
}

bool Page::isActive() {
  return _active;
}

#endif // #if DSP_MODEL!=DSP_DUMMY
