#include <IntCurvesFace_ShapeIntersector.hxx>
#include "presentation/SelectionAppearance.h"
#include "ui/CadViewport.h"
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Prs3d_PointAspect.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <StdSelect_BRepOwner.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QShortcut>
#include <QScopedValueRollback>
#include <BRepAdaptor_Surface.hxx>
#include <algorithm>
#include <cmath>

namespace thecad::ui {
using namespace presentation;
using modeling::SelectionKind;
using modeling::SelectionRef;
namespace {
SelectionKind datumSelectionKind(modeling::DatumKind kind){return kind==modeling::DatumKind::Point?SelectionKind::ReferencePoint:kind==modeling::DatumKind::Axis?SelectionKind::ReferenceAxis:SelectionKind::ReferencePlane;}
}
std::vector<SelectionRef> CadViewport::selection() const {
    if(!m_sketchEditing){
        auto result=m_selection.items();if(!result.empty())return result;
        if(!m_selectedDatum.empty()){const auto d=std::find_if(m_datums.begin(),m_datums.end(),[&](const auto& d){return d.id==m_selectedDatum;});if(d!=m_datums.end())result.push_back({datumSelectionKind(d->geometry.kind),d->id});}
        else if(m_selectedPlane)result.push_back({SelectionKind::OriginPlane,{}, {},modeling::PointRole::First,int(*m_selectedPlane)});
        else if(m_selectedSolidFace)result.push_back({SelectionKind::Face,m_bodyFeatureId,{},modeling::PointRole::First,m_selectedSolidFace,m_shapeGeneration});
        return result;
    }
    // Existing solver/dimension gestures retain their stable sketch IDs. This
    // adapter exposes the same typed references to every command and inspector.
    std::vector<SelectionRef> result;
    if(!m_selectedRelation.empty())result.push_back({SelectionKind::Relation,m_sketchFeatureId,m_selectedRelation});
    else {
        if(m_selectedPoint)result.push_back({SelectionKind::SketchPoint,m_sketchFeatureId,m_selectedPoint->entityId,m_selectedPoint->role});
        for(const auto& id:m_selectedEntities)if(!m_selectedPoint||m_selectedPoint->entityId!=id)result.push_back({SelectionKind::SketchEntity,m_sketchFeatureId,id});
        for(const auto& id:m_selectedDimensions)result.push_back({SelectionKind::Dimension,m_sketchFeatureId,id});
    }
    return result;
}
void CadViewport::beginTreeSelection(){if(m_dispatchingSelection)return;m_treeBodySelected=false;m_selection.clear();m_nativeSelections.clear();refreshNativeSelection();previewCandidate({});}
void CadViewport::setSelectionFilters(std::uint32_t filters,bool enabled){
    m_selectionPolicy={filters&modeling::FilterAll,enabled};
    previewCandidate({});m_hoveredEntity.clear();m_hoveredDimension.clear();m_hoveredRelation.clear();m_hoveredPoint.reset();m_hoveredDatum.clear();m_hoveredPlane.reset();
    updatePointPresentation();updateConstraintPresentation();updateDatumStyles();updatePlaneStyles();update();
}
void CadViewport::invalidateBodySelection(){
    if(isReady())for(auto& item:m_selectionPresentations)m_graphics.context()->Remove(item,Standard_False);m_selectionPresentations.clear();
    ++m_shapeGeneration;m_selection.invalidateTopology(m_shapeGeneration);m_nativeSelections.clear();m_selectedSolidFace=0;
    if(isReady())m_graphics.context()->ClearSelected(Standard_False);
    previewCandidate({});if(m_selectOtherMenu)m_selectOtherMenu->close();
}
void CadViewport::prepareBodyPicking(){
    if(m_pickCacheGeneration==m_shapeGeneration)return;m_pickCacheGeneration=m_shapeGeneration;
    m_selectionFaces.Clear();m_selectionEdges.Clear();m_selectionVertices.Clear();m_selectionBodies.Clear();m_seamEdges.clear();m_artificialVertices.clear();
    if(m_shape.IsNull())return;
    TopExp::MapShapes(m_shape,TopAbs_FACE,m_selectionFaces);TopExp::MapShapes(m_shape,TopAbs_EDGE,m_selectionEdges);TopExp::MapShapes(m_shape,TopAbs_VERTEX,m_selectionVertices);TopExp::MapShapes(m_shape,TopAbs_SOLID,m_selectionBodies);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces,vertexEdges;
    TopExp::MapShapesAndAncestors(m_shape,TopAbs_EDGE,TopAbs_FACE,edgeFaces);TopExp::MapShapesAndAncestors(m_shape,TopAbs_VERTEX,TopAbs_EDGE,vertexEdges);
    for(int i=1;i<=m_selectionEdges.Extent();++i){const auto edge=TopoDS::Edge(m_selectionEdges(i));if(!edgeFaces.Contains(edge))continue;for(TopTools_ListIteratorOfListOfShape it(edgeFaces.FindFromKey(edge));it.More();it.Next())if(BRep_Tool::IsClosed(edge,TopoDS::Face(it.Value()))){m_seamEdges.insert(i);break;}}
    for(int i=1;i<=m_selectionVertices.Extent();++i){const auto& vertex=m_selectionVertices(i);if(!vertexEdges.Contains(vertex))continue;std::set<int> physicalEdges;
        for(TopTools_ListIteratorOfListOfShape it(vertexEdges.FindFromKey(vertex));it.More();it.Next()){const int edge=m_selectionEdges.FindIndex(it.Value());if(!m_seamEdges.count(edge))physicalEdges.insert(edge);}
        // A periodic circle has an implementation vertex at its seam, not a
        // visible corner. Keep genuine wire endpoints and multi-edge junctions.
        if(physicalEdges.empty()||(physicalEdges.size()==1&&BRep_Tool::IsClosed(m_selectionEdges(*physicalEdges.begin()))))m_artificialVertices.insert(i);
    }
}
void CadViewport::clearSelection(){
    m_treeBodySelected=false;m_selection.clear();m_nativeSelections.clear();m_selectedSolidFace=0;m_selectedDatum.clear();m_selectedPlane.reset();
    m_hoveredDatum.clear();m_hoveredPlane.reset();m_hoveredPoint.reset();m_hoveredEntity.clear();m_hoveredDimension.clear();m_hoveredRelation.clear();
    m_selectedPoint.reset();m_selectedEntities.clear();m_selectedDimensions.clear();m_selectedEntity.clear();m_selectedRelation.clear();
    refreshNativeSelection();
    previewCandidate({});updateDatumStyles();updatePlaneStyles();updatePointPresentation();updateConstraintPresentation();
    emit selectionChanged();if(m_sketchEditing)emit sketchSelectionChanged();else {emit solidFaceSelected(0);emit datumSelected({});}update();
}
std::vector<CadViewport::PickCandidate> CadViewport::selectionCandidates(const QPointF& position,bool hidden){
    std::vector<PickCandidate> candidates;
    if(!isReady())return candidates;
    synchronizeSurface();
    const auto append=[&](modeling::SelectionCandidate value,const TopoDS_Shape& shape=TopoDS_Shape(),const Handle(SelectMgr_EntityOwner)& owner={},const std::optional<gp_Pnt>& hit={}){candidates.push_back({value,shape,owner,hit});};
    if(m_sketchEditing && m_sketch){
        for(const auto& entry:modeling::sketchPoints(*m_sketch)){
            const double distance=QLineF(position,screenPoint(entry.second)).length();if(distance>m_pickingTolerance)continue;
            if(entry.first.role==modeling::PointRole::Center){const auto e=std::find_if(m_sketch->entities.begin(),m_sketch->entities.end(),[&](const auto& e){return e.id==entry.first.entityId;});const double radius=QLineF(screenPoint(e->first),screenPoint({e->first.x+e->radius,e->first.y})).length();if(std::abs(distance-radius)<distance)continue;}
            append({{SelectionKind::SketchPoint,m_sketchFeatureId,entry.first.entityId,entry.first.role},distance});
        }
        for(const auto& item:sketchEntityCandidates(position))append(item);
        for(const auto& dimension:dimensionCandidates(position))append(dimension);
        if(m_selectionPolicy.allows(SelectionKind::Relation))for(const auto& hit:m_relationHits)if(hit.bounds.adjusted(-2,-2,2,2).contains(position))append({{SelectionKind::Relation,m_sketchFeatureId,hit.id}});
    }else{
        // OCCT intersects sensitive topology, including exact trimmed circles
        // and triangulated faces. It never tests a projected bounding box alone.
        if(!m_presentation.IsNull() && m_graphics.context()->IsDisplayed(m_presentation)){
            prepareBodyPicking();
            const auto pixel=nativePosition(position);const auto selector=m_graphics.context()->MainSelector();
            // OCCT's point frustum uses a full width, while our tolerance is a
            // cursor radius. Ensure custom sensitivity is actually installed:
            // 7.9 can otherwise early-return when the adaptive maximum matches.
            const int width=qMax(1,qRound(2*m_pickingTolerance*devicePixelRatioF()));
            if(selector->CustomPixelTolerance()!=width){selector->SetPixelTolerance(width+1);selector->SetPixelTolerance(width);}
            selector->Pick(pixel.x(),pixel.y(),m_graphics.view());
            double frontDepth=1e100;
            for(int n=1;n<=selector->NbPicked();++n){
                const auto owner=Handle(StdSelect_BRepOwner)::DownCast(selector->Picked(n));
                if(owner.IsNull()||owner->Selectable()!=m_presentation||!owner->HasShape())continue;
                const auto shape=owner->Shape();SelectionKind kind;TopAbs_ShapeEnum type=shape.ShapeType();
                if(type==TopAbs_VERTEX)kind=SelectionKind::Vertex;else if(type==TopAbs_EDGE)kind=SelectionKind::Edge;else if(type==TopAbs_FACE)kind=SelectionKind::Face;else if(type==TopAbs_SOLID)kind=SelectionKind::Body;else continue;
                const auto& map=type==TopAbs_FACE?m_selectionFaces:type==TopAbs_EDGE?m_selectionEdges:type==TopAbs_VERTEX?m_selectionVertices:m_selectionBodies;const int index=map.FindIndex(shape);if(index<=0)continue;
                if((type==TopAbs_EDGE&&m_seamEdges.count(index))||(type==TopAbs_VERTEX&&m_artificialVertices.count(index)))continue;
                auto hit=type==TopAbs_VERTEX?BRep_Tool::Pnt(TopoDS::Vertex(shape)):selector->PickedPoint(n);bool onFace=false;
                if(type==TopAbs_FACE||type==TopAbs_SOLID){double x,y,z,dx,dy,dz;m_graphics.view()->ConvertWithProj(pixel.x(),pixel.y(),x,y,z,dx,dy,dz);IntCurvesFace_ShapeIntersector intersection;intersection.Load(shape,1e-7);intersection.Perform(gp_Lin(gp_Pnt(x,y,z),gp_Dir(dx,dy,dz)),-1e15,1e15);double nearest=1e100;for(int k=1;k<=intersection.NbPnt();++k){const auto p=intersection.Pnt(k);const double d=gp_Vec(m_graphics.view()->Camera()->Eye(),p).Dot(gp_Vec(m_graphics.view()->Camera()->Direction()));if(d>=0&&d<nearest){nearest=d;hit=p;onFace=true;}}}
                const double depth=gp_Vec(m_graphics.view()->Camera()->Eye(),hit).Dot(gp_Vec(m_graphics.view()->Camera()->Direction()));
                int x,y;m_graphics.view()->Convert(hit.X(),hit.Y(),hit.Z(),x,y);const double distance=onFace?0:QLineF(position,QPointF(x/devicePixelRatioF(),y/devicePixelRatioF())).length();
                if(distance>m_pickingTolerance)continue;
                frontDepth=std::min(frontDepth,depth);
                append({{kind,m_bodyFeatureId,{},modeling::PointRole::First,index,m_shapeGeneration},distance,depth},shape,owner,hit);
            }
            // Tiny depth slack permits shared front edges/vertices. Back edges
            // remain unavailable on normal clicks even with an edge-only filter.
            const double slack=std::max(1e-6,m_graphics.view()->Camera()->Scale()/std::max(1,height())*2.0);
            for(auto& candidate:candidates)candidate.value.occluded=candidate.value.depth>frontDepth+slack;
        }
        if(!m_referenceCandidateKind){
            const auto datumIds=datumIdsAt(position);
            for(const auto& datum:m_datums){
                // Reuse the existing exact datum geometry hit tests, one stable
                // entity at a time so overlapping references reach Select Other.
                if(std::find(datumIds.begin(),datumIds.end(),datum.id)==datumIds.end())continue;
                append({{datumSelectionKind(datum.geometry.kind),datum.id},0,0},modeling::datumDisplayShape(datum.geometry,50));
            }
            if(m_planesVisible){
                const auto pixel=nativePosition(position);double x,y,z,dx,dy,dz;m_graphics.view()->ConvertWithProj(pixel.x(),pixel.y(),x,y,z,dx,dy,dz);
                const gp_Dir direction(dx,dy,dz);const gp_Pnt projected(x,y,z);const double depth=gp_Vec(m_graphics.view()->Camera()->Eye(),projected).Dot(gp_Vec(direction));const gp_Lin ray(projected.Translated(-gp_Vec(direction)*depth),direction);
                const auto nearest=modeling::pickReferencePlane(ray,m_planeHalfExtent);
                for(const auto type:{modeling::SketchPlane::XY,modeling::SketchPlane::XZ,modeling::SketchPlane::YZ})if(modeling::pickReferencePlane(ray,m_planeHalfExtent,type))
                    append({{SelectionKind::OriginPlane,{}, {},modeling::PointRole::First,int(type)},0,nearest&&nearest->plane==type?0.0:1.0},modeling::referencePlaneFace(type,m_planeHalfExtent));
            }
        }
    }
    auto policy=m_selectionPolicy;
    if(m_selectingPlane){ // A support command only accepts planes or faces.
        candidates.erase(std::remove_if(candidates.begin(),candidates.end(),[](const auto& c){const auto k=c.value.reference.kind;if(k==SelectionKind::Face)return BRepAdaptor_Surface(TopoDS::Face(c.shape)).GetType()!=GeomAbs_Plane;return k!=SelectionKind::ReferencePlane&&k!=SelectionKind::OriginPlane;}),candidates.end());
    }
    std::vector<modeling::SelectionCandidate> values;for(const auto& c:candidates)values.push_back(c.value);
    const auto ordered=policy.order(std::move(values),hidden);std::vector<PickCandidate> result;
    for(const auto& value:ordered){const auto found=std::find_if(candidates.begin(),candidates.end(),[&](const auto& c){return c.value.reference==value.reference;});result.push_back(*found);}return result;
}
void CadViewport::previewCandidate(const std::optional<PickCandidate>& candidate){
    if(isReady()&&!m_selectionHover.IsNull())m_graphics.context()->Remove(m_selectionHover,Standard_False);m_selectionHover.Nullify();
    if(m_sketchEditing){
        m_hoveredPoint.reset();m_hoveredEntity.clear();m_hoveredDimension.clear();m_hoveredRelation.clear();
        if(candidate){const auto& r=candidate->value.reference;
            if(r.kind==SelectionKind::SketchEntity)m_hoveredEntity=r.entityId;
            else if(r.kind==SelectionKind::SketchPoint)m_hoveredPoint=modeling::SketchPointRef{r.entityId,r.role};
            else if(r.kind==SelectionKind::Dimension)m_hoveredDimension=r.entityId;
            else if(r.kind==SelectionKind::Relation)m_hoveredRelation=r.entityId;
        }updatePointPresentation();updateConstraintPresentation();
    }
    if(!candidate||candidate->shape.IsNull()||!isReady())return;
    // Selection wins over hover: keep the complete selected surface blue.
    const auto& reference=candidate->value.reference;
    if(std::find(m_selection.items().begin(),m_selection.items().end(),reference)!=m_selection.items().end()
       ||(reference.kind==SelectionKind::Face&&reference.index==m_selectedSolidFace)
       ||(reference.kind==SelectionKind::OriginPlane&&m_selectedPlane&&reference.index==int(*m_selectedPlane))
       ||(!modeling::isTopology(reference.kind)&&!m_selectedDatum.empty()&&reference.featureId==m_selectedDatum))return;
    m_selectionHover=new AIS_Shape(selectionOutline(candidate->shape));m_selectionHover->SetDisplayMode(AIS_WireFrame);m_selectionHover->SetColor(hoverOrange());m_selectionHover->SetWidth(3*devicePixelRatioF());
    if(candidate->value.occluded)m_selectionHover->SetZLayer(Graphic3d_ZLayerId_Topmost);
    m_selectionHover->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_POINT,hoverOrange(),9*devicePixelRatioF()));
    m_graphics.context()->Display(m_selectionHover,AIS_WireFrame,-1,Standard_False);update();
}
void CadViewport::updateSelectionHover(const QPointF& point){
    auto candidates=selectionCandidates(point);previewCandidate(candidates.empty()?std::optional<PickCandidate>{}:candidates.front());
    m_hoveredDatum.clear();m_hoveredPlane.reset();
    if(!candidates.empty()){const auto& r=candidates.front().value.reference;
        if(r.kind==SelectionKind::ReferencePlane||r.kind==SelectionKind::ReferenceAxis||r.kind==SelectionKind::ReferencePoint)m_hoveredDatum=r.featureId;
        if(r.kind==SelectionKind::OriginPlane)m_hoveredPlane=modeling::SketchPlane(r.index);
    }updateDatumStyles();updatePlaneStyles();update();
}
void CadViewport::refreshNativeSelection(){
    if(!isReady())return;m_graphics.context()->ClearSelected(Standard_False);
    for(const auto& item:m_nativeSelections)if(std::find(m_selection.items().begin(),m_selection.items().end(),item.value.reference)!=m_selection.items().end()&&!item.owner.IsNull())m_graphics.context()->AddOrRemoveSelected(item.owner,Standard_False);
    for(auto& item:m_selectionPresentations)m_graphics.context()->Remove(item,Standard_False);m_selectionPresentations.clear();
    const auto display=[&](const TopoDS_Shape& shape){if(shape.IsNull())return;Handle(AIS_Shape) item=new AIS_Shape(shape);styleSelectedShape(item,devicePixelRatioF());const bool filled=shape.ShapeType()==TopAbs_FACE||shape.ShapeType()==TopAbs_SOLID||shape.ShapeType()==TopAbs_COMPOUND;m_graphics.context()->Display(item,filled?AIS_Shaded:AIS_WireFrame,-1,Standard_False);m_selectionPresentations.push_back(item);};
    if(m_treeBodySelected)display(m_shape);else for(const auto& item:m_nativeSelections)if(std::find(m_selection.items().begin(),m_selection.items().end(),item.value.reference)!=m_selection.items().end())display(item.shape);
    if(m_selectedSolidFace>0&&m_nativeSelections.empty()){TopTools_IndexedMapOfShape faces;TopExp::MapShapes(m_shape,TopAbs_FACE,faces);if(m_selectedSolidFace<=faces.Extent())display(faces(m_selectedSolidFace));}
    updateDatumStyles();updatePlaneStyles();update();
}
void CadViewport::setTreeReferenceSelection(const std::vector<SelectionRef>& refs){if(m_dispatchingSelection)return;m_selection.clear();for(const auto& ref:refs)m_selection.select(ref,true);refreshNativeSelection();}
void CadViewport::setTreeBodySelected(bool selected){m_treeBodySelected=selected;refreshNativeSelection();}
void CadViewport::selectCandidate(const PickCandidate& item,bool toggle){
    const QScopedValueRollback<bool> dispatch(m_dispatchingSelection,true);
    const auto& ref=item.value.reference;
    if(modeling::isTopology(ref.kind)&&ref.generation!=m_shapeGeneration)return;
    if(m_sketchEditing){
        if(ref.kind==SelectionKind::Dimension)selectDimension(ref.entityId,toggle);
        else if(ref.kind==SelectionKind::Relation)selectSketchRelation(ref.entityId);
        else {
            if(!toggle){m_selectedEntities.clear();m_selectedDimensions.clear();}
            const auto found=std::find(m_selectedEntities.begin(),m_selectedEntities.end(),ref.entityId);
            if(toggle&&found!=m_selectedEntities.end())m_selectedEntities.erase(found);else if(!ref.entityId.empty())m_selectedEntities.push_back(ref.entityId);
            m_selectedEntity=m_selectedEntities.empty()?"":m_selectedEntities.back();m_selectedRelation.clear();m_selectedPoint.reset();
            if(ref.kind==SelectionKind::SketchPoint)m_selectedPoint=modeling::SketchPointRef{ref.entityId,ref.role};
            emit sketchEntitySelected(QString::fromStdString(m_selectedEntity));if(m_selectedPoint)emit sketchPointSelected(QString::fromStdString(ref.entityId),int(ref.role));
            updatePointPresentation();updateConstraintPresentation();emit sketchSelectionChanged();
        }
        emit selectionChanged();return;
    }
    if(!toggle)m_nativeSelections.clear();m_selection.select(ref,toggle);
    if(!item.owner.IsNull()&&std::none_of(m_nativeSelections.begin(),m_nativeSelections.end(),[&](const auto& c){return c.value.reference==ref;}))m_nativeSelections.push_back(item);
    m_selectedSolidFace=0;m_selectedDatum.clear();m_selectedPlane.reset();
    for(const auto& selected:m_selection.items()){
        if(selected.kind==SelectionKind::ReferencePlane||selected.kind==SelectionKind::ReferenceAxis||selected.kind==SelectionKind::ReferencePoint)m_selectedDatum=selected.featureId;
        if(selected.kind==SelectionKind::OriginPlane)m_selectedPlane=modeling::SketchPlane(selected.index);
    }
    if(m_selection.items().size()==1&&m_selection.items()[0].kind==SelectionKind::Face)m_selectedSolidFace=m_selection.items()[0].index;
    refreshNativeSelection();previewCandidate({});
    emit selectionChanged();
    // Compatibility notifications supply a support only for a single valid
    // selection; edge/vertex clicks never masquerade as a planar face.
    if(m_selection.items().size()==1){
        if(m_selectedSolidFace)emit solidFaceSelected(m_selectedSolidFace);
        else if(!m_selectedDatum.empty())emit datumSelected(QString::fromStdString(m_selectedDatum));
        else if(m_selectedPlane)emit referencePlaneSelected(*m_selectedPlane);
        else {emit solidFaceSelected(0);emit datumSelected({});}
    }else {emit solidFaceSelected(0);emit datumSelected({});}
}
void CadViewport::showSelectOther(const QPoint& point){
    if(m_tools.drawing()||m_dimensionDrag||m_pointDrag||m_regionSelecting||!m_referenceCandidates.empty())return;
    const auto candidates=selectionCandidates(point,true);if(candidates.empty())return;
    if(m_selectOtherMenu)m_selectOtherMenu->close();auto* menu=new QMenu(this);menu->setObjectName("selectOtherMenu");menu->setAttribute(Qt::WA_DeleteOnClose);m_selectOtherMenu=menu;
    for(const auto& candidate:candidates){const auto& r=candidate.value.reference;
        QString label=QString::fromLatin1(modeling::selectionKindName(r.kind));
        if(r.kind==SelectionKind::OriginPlane)label+=" · "+QString::fromLatin1(modeling::referencePlaneName(modeling::SketchPlane(r.index)));
        else if(r.index>0)label+=" "+QString::number(r.index);if(!r.entityId.empty())label+=" · "+QString::fromStdString(r.entityId);else if(!r.featureId.empty())label+=" · "+QString::fromStdString(r.featureId);
        if(candidate.value.occluded)label+=" (behind)";
        auto* action=menu->addAction(label);connect(action,&QAction::hovered,this,[this,candidate]{previewCandidate(candidate);});
        connect(action,&QAction::triggered,this,[this,candidate]{selectCandidate(candidate);});
    }
    auto* cycle=new QShortcut(QKeySequence(Qt::Key_Tab),menu);cycle->setContext(Qt::WidgetWithChildrenShortcut);
    connect(cycle,&QShortcut::activated,menu,[menu]{const auto actions=menu->actions();const int index=actions.indexOf(menu->activeAction());menu->setActiveAction(actions[(index+1)%actions.size()]);});
    connect(menu,&QMenu::aboutToHide,this,[this]{previewCandidate({});update();});menu->popup(mapToGlobal(point));menu->setActiveAction(menu->actions().front());
}
void CadViewport::contextMenuEvent(QContextMenuEvent* event){
    if(m_tools.drawing()||m_dimensionDrag||m_pointDrag||m_regionSelecting||!m_referenceCandidates.empty()){event->ignore();return;}
    if(!m_contextClick&&event->reason()!=QContextMenuEvent::Keyboard){event->ignore();return;}
    m_contextClick=false;auto* menu=new QMenu(this);menu->setObjectName("geometryContextMenu");menu->setAttribute(Qt::WA_DeleteOnClose);
    const auto position=event->reason()==QContextMenuEvent::Keyboard?rect().center():event->pos();
    if(event->reason()!=QContextMenuEvent::Keyboard){const auto candidates=selectionCandidates(position);if(!candidates.empty()){
        const auto current=selection();const auto& ref=candidates.front().value.reference;
        const bool alreadySelected=std::any_of(current.begin(),current.end(),[&](const auto& r){return r==ref||(!r.entityId.empty()&&r.featureId==ref.featureId&&r.entityId==ref.entityId);});
        if(!alreadySelected)selectCandidate(candidates.front());
    }}
    auto* other=menu->addAction("Select Other…");other->setEnabled(!selectionCandidates(position,true).empty());connect(other,&QAction::triggered,this,[this,position]{showSelectOther(position);});
    emit constructionContextRequested(menu);
    menu->popup(mapToGlobal(position));event->accept();
}
}
