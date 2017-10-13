cd /genomics/scratch
for file in newparted*.sam; do 
	bamname=$(echo $file | awk -F. '{print $1}'); 
	bamname="$bamname""$(hostname)"".bam" ; 
	/genomics/samtools/samtools sort $file -o $bamname -@1 
done

rm /genomics/scratch/newparted*.sam

/genomics/samtools/samtools merge sorted.bam newparted*.bam  -f 

