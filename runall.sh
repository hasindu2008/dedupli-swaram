make
rsync -avz /home/odroid/sorting_framework/main odroid@odroid03.cse.unsw.edu.au:/home/odroid/sorting_framework/main
rsync -avz /home/odroid/sorting_framework/main odroid@odroid04.cse.unsw.edu.au:/home/odroid/sorting_framework/main
rsync -avz /home/odroid/sorting_framework/main odroid@odroid05.cse.unsw.edu.au:/home/odroid/sorting_framework/main
./main &
echo "Press any key to continue"
read p
ansible-playbook ansible.yml
