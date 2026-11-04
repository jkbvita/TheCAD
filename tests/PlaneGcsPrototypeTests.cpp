#include "GCS.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void closeValue(double a,double b){if(std::abs(a-b)>=1e-6)throw std::runtime_error("PlaneGCS numeric residual: "+std::to_string(a)+" expected "+std::to_string(b));}
int main(){try{
 double x0=0,y0=0,x1=50,y1=0,x2=50,y2=30,x3=0,y3=30,width=50,height=30;
 for(double target:{50.0,80.0}){
 width=target;GCS::System system;GCS::VEC_pD unknowns{&x1,&y1,&x2,&y2,&x3,&y3};
 GCS::Point p0(&x0,&y0),p1(&x1,&y1),p2(&x2,&y2),p3(&x3,&y3);
 system.addConstraintHorizontal(p0,p1,1);system.addConstraintVertical(p1,p2,2);system.addConstraintHorizontal(p2,p3,3);system.addConstraintVertical(p3,p0,4);
 system.addConstraintDifference(&x0,&x1,&width,5);system.addConstraintDifference(&y0,&y3,&height,6);
 system.declareUnknowns(unknowns);system.initSolution();require(system.solve()==GCS::SolveStatus::Success,"Rectangle prototype solve");system.applySolution();system.diagnose();closeValue(x1,target);closeValue(x2,target);closeValue(y2,30);closeValue(x3,0);require(system.dofsNumber()==0,"Fully constrained rectangle diagnosis");
 }
 double x=30,y=40,length=50;
 for(const auto target:{std::pair<double,double>{30,40},{40,60}}){x=target.first;y=target.second;GCS::System system;GCS::Point a(&x0,&y0),b(&x,&y);GCS::VEC_pD unknowns{&x,&y};system.addConstraintP2PDistance(a,b,&length,7);system.declareUnknowns(unknowns);system.initSolution();require(system.solve()==GCS::SolveStatus::Success,"Diagonal length/drag prototype");system.applySolution();closeValue(std::hypot(x,y),50);system.diagnose();require(system.dofsNumber()==1,"Partially constrained line diagnosis");}
 double cx=0,cy=0,r=3,diameter=14;GCS::Circle circle;circle.center=GCS::Point(&cx,&cy);circle.rad=&r;
 {GCS::System system;GCS::VEC_pD unknowns{&cx,&cy,&r};system.addConstraintCircleDiameter(circle,&diameter,10);system.declareUnknowns(unknowns);system.initSolution();require(system.solve()==GCS::SolveStatus::Success,"Circle diameter prototype");system.applySolution();closeValue(r,7);}
 {GCS::System system;double radius=7;GCS::VEC_pD unknowns{&r};system.addConstraintCircleRadius(circle,&radius,11);system.addConstraintCircleDiameter(circle,&diameter,12);system.declareUnknowns(unknowns);system.initSolution();system.solve();system.diagnose();GCS::VEC_I tags;system.getRedundant(tags);require(!tags.empty(),"Redundant driving constraint diagnosis");}
 {GCS::System system;double radius=5;GCS::VEC_pD unknowns{&r};system.addConstraintCircleRadius(circle,&radius,13);system.addConstraintCircleDiameter(circle,&diameter,14);system.declareUnknowns(unknowns);system.initSolution();const auto status=system.solve();system.diagnose();GCS::VEC_I tags;system.getConflicting(tags);require(status!=GCS::SolveStatus::Success || !tags.empty(),"Conflicting dimensions must diagnose");}
 std::cout<<"PASS: standalone PlaneGCS rectangle, driving dimensions, drag, radius/diameter, DOF, redundant/conflicting prototype\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
