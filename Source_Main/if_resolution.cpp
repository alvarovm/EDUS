// if the contdition for printing is true
 Emax = sqrt(EF_pr[0][0]*EF_pr[0][0] + EF_pr[0][1]*EF_pr[0][1] + EF_pr[0][2]*EF_pr[0][2]); // electric field modulus

#pragma omp master
{   
    
    if ((rank_ == root_rank) and ( it % (10*it_resolution) == 0))
    {
    cout << " it: " << it << " time: " << time_loop*time_au_fs << " fs, time fin: " << t_fin*time_au_fs << " fs" << endl; 
    }
    
    MPI_Barrier(MPI_COMM_WORLD);

   if(rank_==0){
        PrintEF(fp_E, time_loop, EF_pr);
    }
}

print_matrices_in_kspace = false;
if (    
        (( it % (100*it_resolution) == 0) )
        and (Coulomb_set.Print_new_band_dispersion or Diff_Eq.PrintPopulation) // if there are flag to print any type of matrices 
        and ((time_loop* time_au_fs) > (-0.2)) 
        // and (Emax < E_eps) 
        ){
            print_matrices_in_kspace = true;
            #pragma omp master
            {
                icont++; // counter for snapshots
                if (rank_ == root_rank) {
                    t_Ek <<   time_loop*time_au_fs << endl;
                }
            }
            
    }


if (Coulomb_set.Coulomb_calc) {

    // creates a file that contains new bands energies 
    if (print_matrices_in_kspace){
        if (rank_ == root_rank){ // only root rank writes to file
            #pragma omp master
            {   
                
                Coulomb_set.Ek_file.close(); // close previously opened
                string name_file; 
                stringstream sname;
                sname.seekp(0,ios::beg); 
                sname << icont;
                name_file= "Output/Edia_" + sname.str() + ".txt";
                Coulomb_set.Ek_file.open(name_file.c_str());
                
                
            }
        }
    }

    PrintLossesMPI_exciton(fp_Loss, time_loop,
    Ncv, Nb[0], (Nb[0]+Nb[1]),
    P0, Coulomb_set, OMP_private, trig_k_omp, EF_pr,
    root_rank, rank_, print_matrices_in_kspace, n_cond, P_cond_max_loc_mpi);

    // #pragma omp master
    // {
    //     MPI_Bcast(& n_cond, 1, MPI_DOUBLE, root_rank, MPI_COMM_WORLD);
    //     Coulomb_set.n_cond = n_cond;
    // }
    
    // #pragma omp master
    //     {
    //     PrintLossesMPI(nk, nktot, fp_Loss, 
    //     Nb[0], (Nb[0]+Nb[1]), 
    //     P0, time_loop, Unitary, n_cond, P_cond_max_loc_mpi);
    //     }
    #pragma omp barrier

   




 } else{
    PrintLossesMPI_exciton(fp_Loss, time_loop,
    Ncv, Nb[0], (Nb[0]+Nb[1]),
    P0, Coulomb_set, OMP_private, trig_k_omp, EF_pr,
    root_rank, rank_, print_matrices_in_kspace, n_cond, P_cond_max_loc_mpi);
     // #pragma omp master
     //    {
     //    PrintLossesMPI(nk, nktot, fp_Loss, 
     //    Nb[0], (Nb[0]+Nb[1]), 
     //    P0, time_loop, Unitary, n_cond, P_cond_max_loc_mpi);
     //    }
    #pragma omp barrier
 }
   

if (Coulomb_set.Print_new_band_dispersion or Diff_Eq.PrintPopulation){
    #pragma omp master
    {
        if (icont < 1){ // warning: function works only
        // inside master omp thread 
            print_k_MPI(kpt, nk); // print k- grid
        }    
        // if (print_matrices_in_kspace ){
        //     icont++;
        // }
       
                       
    }
}


if(iCurrent)
{


    // calculate new energy gradioent because we renormed hamiltonian
    if(Coulomb_set.Coulomb_calc){
        int ik; // position in shared variable

        //gather all local arrays Hk_renorm into one shared array
        for (int ik_pr = 0; ik_pr < OMP_private.lenght_k; ik_pr++) // loop over position in private variable
        {
            ik = ik_pr + OMP_private.begin_count; // coordinate in global array
            for (int ic=0; ic<Ncv; ic++){ //  over bands
                for (int jc=ic; jc<Ncv; jc++){ //
                    Coulomb_set.Hk_renorm_shared[ik][ic][jc] = OMP_private.Hk_renorm[ik_pr][ic][jc];
                }
            }
        }

        // exchange the borders of Coulomb_set.Hk_renorm_shared to calculate gradient:
        #pragma omp barrier
        #pragma omp master
        { // shared variables, only master change it
            MPI_Barrier(MPI_COMM_WORLD);
            Mpi_communication(Coulomb_set.Hk_renorm_shared,  message);
            MPI_Barrier(MPI_COMM_WORLD);
            GradientEnergy.fill(0);
        }
        #pragma omp barrier

        

        // calculate new energy gradient
        for(int ik_pr = 0; ik_pr < OMP_private.lenght_k; ik_pr++){
            ik = ik_pr + OMP_private.begin_count; // coordinate in global array
            for(int i=0; i<3; i++){
                for(int ic=0; ic<Ncv; ic++){
                    for(int jc=ic; jc<Ncv; jc++){
                        for(int ishell=0; ishell<Bvector.size(); ishell++){
                            for(int ib=0; ib<Bvector[ishell].size(); ib++){
                                GradientEnergy[ik][ic][jc][i] += Weigths[ishell]*Bvector[ishell][ib][i]*
                                (Coulomb_set.Hk_renorm_shared[GradientIndex[ik][ishell][ib]][ic][jc]-Coulomb_set.Hk_renorm_shared[ik][ic][jc]);
                            }
                        }
                        GradientEnergy[ik][jc][ic][i] = conj(GradientEnergy[ik][ic][jc][i]);
                    }
                }
            }
        }
    }

    #pragma omp barrier

    Current1(fp_J1, 
        OMP_private, P0,
        GradientEnergy, time_loop, dk,
        nktot, det_a, J1);

    #pragma omp barrier

    if(Coulomb_set.Coulomb_calc){ // in case of Coulomb correlation, we insert renormed Hamiltonian
        Current2(fp_J2, 
        OMP_private, P0,
        Dipole, Coulomb_set.Hk_renorm_shared, time_loop, dk,
        nktot, det_a, J2);

    }else{
        Current2(fp_J2, 
        OMP_private, P0,
        Dipole, Hamiltonian, time_loop, dk,
        nktot, det_a, J2);
    }
    



}
        
#pragma omp master
 {
    if(iTAbs){
        PrintTransientAbsMPI(nk,nktot,fp_TAbs, P0, time_loop, EF_pr[1], dk, Nb[0], Coord_B::getJ(), Dipole);
    }
 }





// printing maximum values of population and it's derivatives in k
if (Coulomb_set.Coulomb_calc and ((it-iti) % (100*it_resolution) == 0)) {
    #pragma omp barrier
    #pragma omp master
    {   
    
        if (rank_ == root_rank){
            cout << " values of P0_dia[Ncv-1][Ncv-1] ";
        }
    }
    #pragma omp barrier
    print_max_min_conduct(Coulomb_set.P0_dia, 
    OMP_private, Coulomb_set, root_rank);



}



// plot populations
double scaled_t = round( time_loop*time_au_fs* pow(10,4)) / pow(10,4);
if ( Diff_Eq.PrintPopulation and print_matrices_in_kspace)// ((it-iti) % (500*it_resolution) == 0))
{

    // string label = "P_cond_in_eigenstate";
    // double scaled_t = round( time_loop*time_au_fs* pow(10,4)) / pow(10,4);
    // Print_vec3x_MPI(OMP_private.P_eigen, Ncv-1, Ncv-1, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, icont); // icont - the same number as in in Ec

    // label = "P_val_cond_in_eigenstate";
    // Print_vec3x_MPI(OMP_private.P_eigen, Ncv-2, Ncv-1, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, icont); // icont - the same number as in in Ec

    string label = "P_cond_in_Bloch";
    int ik_pr; // transform into Bloch basis
    for (int ik = OMP_private.begin_count; ik < OMP_private.end_count; ik++){ // all local wave vectors
        ik_pr = ik - OMP_private.begin_count;
        
        // function transforms P[ik] with OMP_private.Uk[ik_pr] and writes it to the 
        // OMP_private.P_diag[ik]
        get_P_in_dia_vect(P0,  OMP_private.Uk, OMP_private.P_diag, Ncv, 
            ik, ik_pr, OMP_private);
    }
    double scaled_t = round( time_loop*time_au_fs* pow(10,4)) / pow(10,4);
    for (int ii = (Nb[0]+Nb[1]); ii<Ncv; ii++){
        Print_vec3x_MPI(OMP_private.P_diag, ii, ii, scaled_t,
         OMP_private,
         Coulomb_set,
         label, icont); // icont - the same number as in in Ec
    }

    label = "P_val_cond_in_Bloch";
    Print_vec3x_MPI(OMP_private.P_diag, Ncv-2, Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, icont); // icont - the same number as in in Ec


    label = "P_Wannier_";
    Print_vec3x_MPI(P0, Ncv-1, Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, icont); // icont - the same number as in in Ec

    label = "P_Wannier_";
    Print_vec3x_MPI(P0, Ncv-2, Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, icont); // icont - the same number as in in Ec

    // label = "P_Wannier_v_v";
    // Print_vec3x_MPI(P0, Ncv-2, Ncv-2, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, icont); // icont - the same number as in in Ec

    // label = "P_Wannier_core_core";
    // Print_vec3x_MPI(P0, Ncv-3, Ncv-3, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, icont); // icont - the same number as in in Ec

    // label = "P_Wannier_core_c";
    // Print_vec3x_MPI(P0, Ncv-3, Ncv-1, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, icont); // icont - the same number as in in Ec

    // label = "P_Wannier_core_v";
    // Print_vec3x_MPI(P0, Ncv-3, Ncv-2, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, icont); // icont - the same number as in in Ec





    if(it == 0){
        label = "H_cond";
        Print_vec3x_MPI(Hamiltonian, Ncv-1,Ncv-1, scaled_t,
         OMP_private,
         Coulomb_set,
         label, it);

        label = "H_offdia";
        Print_vec3x_MPI(Hamiltonian, Ncv-2,Ncv-1, scaled_t,
         OMP_private,
         Coulomb_set,
         label, it);

        label = "Uk_cond_11";
        Print_vec3x_MPI(Unitary, Ncv-1,Ncv-1, scaled_t,
         OMP_private,
         Coulomb_set,
         label, it);

        label = "Uk_val";
        Print_vec3x_MPI(Unitary, Ncv-2,Ncv-2, scaled_t,
         OMP_private,
         Coulomb_set,
         label, it);


        label = "Uk10_offdia";
        Print_vec3x_MPI(Unitary, Ncv-1,Ncv-2, scaled_t,
         OMP_private,
         Coulomb_set,
         label, it);

        label = "Uk01_offdia";
        Print_vec3x_MPI(Unitary, Ncv-2,Ncv-1, scaled_t,
         OMP_private,
         Coulomb_set,
         label, it);

       
    }
    
    // for (int nb = 0; nb<Ncv; nb++){
        
    
    //     if (Coulomb_set.Coulomb_calc){
    //         label = "X_Bloch_";
    //         Print_vec3x_MPI(Coulomb_set.Xk_storage, nb, nb, scaled_t,
    //          OMP_private,
    //          Coulomb_set,
    //          label, icont); // icont - the same number as in in Ec
           
    //         // label = "X_Bloch_v_c";
    //         // Print_vec3x_MPI(Coulomb_set.Xk_storage, Ncv-2, Ncv-1, scaled_t,
    //         //  OMP_private,
    //         //  Coulomb_set,
    //         //  label, icont); // icont - the same number as in in Ec

    //     }
    // }

    //     label = "P_cond_in_equilibrium_basis";
    //     Print_vec3x_MPI(OMP_private.P_diag, Ncv-1,Ncv-1, scaled_t,
    //      OMP_private,
    //      Coulomb_set,
    //      label, it);

    //     label = "P_cond_dkx";
    //     Print_vec3x_MPI(Coulomb_set.P_dkx, Ncv-1, Ncv-1, scaled_t,
    //      OMP_private,
    //      Coulomb_set,
    //      label, it);

    //     label = "P_cond_d2kx";
    //     Print_vec3x_MPI(Coulomb_set.P_d2kx, Ncv-1,Ncv-1, scaled_t,
    //      OMP_private,
    //      Coulomb_set,
    //      label, it);
    // }




}


if (it == 0){
    Diagonalize_unitary_vec3x(OMP_private.Hk, H_Eigen, OMP_private);

    string label = "H_Eigen_cond";
    Print_vec3x_MPI(H_Eigen, Ncv-1,Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "H_Eigen_val";
    Print_vec3x_MPI(H_Eigen, Ncv-2,Ncv-2, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "H_Eigen_val_cond";
    Print_vec3x_MPI(H_Eigen, Ncv-2,Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "H_Eigen_";
    Print_vec3x_MPI(H_Eigen, Ncv-3,Ncv-3, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);


    label = "H_Wannier_cond";
    Print_vec3x_MPI(Hamiltonian, Ncv-1,Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "H_Wannier_val_cond";
    Print_vec3x_MPI(Hamiltonian, Ncv-2,Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "H_Wannier_val";
    Print_vec3x_MPI(Hamiltonian, Ncv-2,Ncv-2, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);


    label = "P_Wannier_";
    Print_vec3x_MPI(P0, Ncv-1,Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "P_Wannier_";
    Print_vec3x_MPI(P0, Ncv-1,Ncv-2, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "Uk_";
    Print_vec3x_MPI(Unitary, Ncv-1,Ncv-1, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);

    label = "Uk_";
    Print_vec3x_MPI(Unitary, Ncv-1,Ncv-2, scaled_t,
     OMP_private,
     Coulomb_set,
     label, it);


    // vec2x Xk_W(Ncv,Ncv); // Coulomb term in Wannier Basis
    // vec2x Xk_d(Ncv,Ncv); // Coulomb term in eigenstate Basis
    // int ik; // position in shared variable
    // for (int ik_pr = 0; ik_pr < OMP_private.lenght_k; ik_pr++) // loop over position in private variable
    // {
    //     ik = ik_pr + OMP_private.begin_count; // coordinate in global array
    //     // from diagonal back to wannier basis:
    //     for (int ic=0; ic<Ncv; ic++){// summation over bands   // 
    //         for (int jc=0; jc<Ncv; jc++){ // summation over bands
    //             Xk_d[ic][jc] = H_Eigen[ik][ic][jc];
    //         }
    //     }

    //     get_Xd_in_Wannier_vect(Xk_d, Xk_W, OMP_private.Uk, 
    //                 Ncv,  ik_pr, OMP_private);

    //     for (int ic=0; ic<Ncv; ic++){// summation over bands   // 
    //         for (int jc=0; jc<Ncv; jc++){ // summation over bands
    //             H_Eigen[ik][ic][jc] = Xk_W[ic][jc]; // sorry for this, but don't want to introduce new variable
    //         }
    //     }

    // }

    // label = "H_Wannier_transformed_by_unitary_cond";
    // Print_vec3x_MPI(H_Eigen, Ncv-1,Ncv-1, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, it);


    // label = "H_Wannier_transformed_by_unitary_val_cond";
    // Print_vec3x_MPI(H_Eigen, Ncv-2,Ncv-1, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, it);

    // label = "H_Wannier_transformed_by_unitary_val";
    // Print_vec3x_MPI(H_Eigen, Ncv-2,Ncv-2, scaled_t,
    //  OMP_private,
    //  Coulomb_set,
    //  label, it);



    

}