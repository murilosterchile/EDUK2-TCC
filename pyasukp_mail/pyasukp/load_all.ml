(* load_all.ml *)
(* File used to load within the toplevel loop *)

#load"nums.cma";;
#load"wintpint.cmo";;
#load"wandp.cmo";;
#load"globals.cmo";;
#load"sequences.cmo";;
#load"edukio.cmo";;
#load"eratosthen.cmo";;
#load"datagen.cmo";;
#load"parsecl.cmo";;
open Globals
open Wandp.M;;

#load"chainlist.cmo";;
#load"astore.cmo";;
#load"printseq.cmo";;
#load"dominance.cmo";;
#load"datastructtype.cmo";;
#load"datastruct.cmo";;
#install_printer Printseq.f;;
#load"prepro.cmo";;
#load"select.cmo";;
#load"bounds.cmo";;
#load"init.cmo";;
#load"dynefflist.cmo";;
#load"bandbukp2.cmo";;
#load"slice.cmo";;
#load"eduk.cmo";;
open Wandp.M
open Globals
open Init
open Bounds
open Parsecl
open Datastructtype
open Datastruct;;

let w,p,n,c = Edukio.items_of_file "bug.ukp";;
#trace Bandbukp2.complete;;
bbnewv := true;;
let res2
 =
 Eduk.reduction w p c 10 true true;;




