let est_multiple_de x y = y mod x = 0;;
let rec une_passe p indeterminés res =
match indeterminés with
| [] -> List.rev res
| n :: suite when est_multiple_de p n -> une_passe p suite res
| n :: suite -> une_passe p suite (n::res)

let rec filtre premiers dernier_connu indeterminés =
  match une_passe dernier_connu indeterminés [] with
  | [] -> premiers
  | prochain::reste -> filtre (prochain::premiers) prochain reste

(* Remarquons que la liste des nombres premiers est construite à l'envers *)

let rec enumère deb fin res =
  if deb > fin then res else  enumère deb (fin-1) (fin::res)

let eratosthène nmin nmax = 
  List.rev(filtre [nmin] nmin (enumère (nmin+1) nmax []));;

let gen = eratosthène
